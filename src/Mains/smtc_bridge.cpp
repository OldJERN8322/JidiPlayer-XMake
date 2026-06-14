// smtc_bridge.cpp — WRL, SDK 10.0.19041.0 compatible
#ifdef _WIN32

#include "smtc_bridge.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wrl.h>
#include <wrl/implements.h>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>
#include <windows.foundation.h>
#include <windows.media.h>   // ABI types + __FITypedEventHandler_2_... typedefs
#include <systemmediatransportcontrolsinterop.h>

using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::Media;
using namespace ABI::Windows::Foundation;

SmtcBridge g_Smtc;

// ── SDK-generated handler typedefs (match exactly what add_*Pressed takes) ───
// These __FI... names come straight from windows.media.h and are always present.
using BtnHandler  = __FITypedEventHandler_2_Windows__CMedia__CSystemMediaTransportControls_Windows__CMedia__CSystemMediaTransportControlsButtonPressedEventArgs;
using SeekHandler = __FITypedEventHandler_2_Windows__CMedia__CSystemMediaTransportControls_Windows__CMedia__CPlaybackPositionChangeRequestedEventArgs;

// ── Impl ─────────────────────────────────────────────────────────────────────
struct SmtcBridge::Impl {
    ComPtr<ISystemMediaTransportControls>                smtc;
    ComPtr<ISystemMediaTransportControls2>               smtc2;
    ComPtr<ISystemMediaTransportControlsDisplayUpdater>  updater;
    SmtcCallbacks                                        cb;
    EventRegistrationToken                               tokenButton{};
    EventRegistrationToken                               tokenSeek{};
    bool                                                 seekRegistered = false;
};

SmtcBridge::SmtcBridge()  = default;
SmtcBridge::~SmtcBridge() { Shutdown(); }

bool SmtcBridge::Init(SmtcCallbacks callbacks) {
    impl = new Impl();
    impl->cb = std::move(callbacks);

    // ── Acquire SMTC via desktop interop ─────────────────────────────────────
    ComPtr<ISystemMediaTransportControlsInterop> interop;
    HRESULT hr = GetActivationFactory(
        HStringReference(RuntimeClass_Windows_Media_SystemMediaTransportControls).Get(),
        &interop);
    if (FAILED(hr)) { delete impl; impl = nullptr; return false; }

    // GetForegroundWindow() is safe here because Init() is called right after
    // the Raylib window is created and shown.  For a more robust approach,
    // store the HWND from GetWindowHandle() (raylib.h) at the call site and
    // pass it in, but this works for the single-window case.
    HWND hwnd = GetForegroundWindow();
    hr = interop->GetForWindow(hwnd, IID_PPV_ARGS(&impl->smtc));
    if (FAILED(hr)) { delete impl; impl = nullptr; return false; }

    // QI for the v2 interface (timeline + seek event — optional, graceful if absent)
    impl->smtc.As(&impl->smtc2);

    // ── Enable buttons ────────────────────────────────────────────────────────
    impl->smtc->put_IsEnabled(true);
    impl->smtc->put_IsPlayEnabled(true);
    impl->smtc->put_IsPauseEnabled(true);
    impl->smtc->put_IsStopEnabled(true);
    impl->smtc->put_IsNextEnabled(false);
    impl->smtc->put_IsPreviousEnabled(false);

    // ── Button handler ────────────────────────────────────────────────────────
    auto btnHandler = Callback<BtnHandler>(
        [this](ISystemMediaTransportControls*,
               ISystemMediaTransportControlsButtonPressedEventArgs* args) -> HRESULT {
            SystemMediaTransportControlsButton btn{};
            if (FAILED(args->get_Button(&btn))) return S_OK;
            switch (btn) {
                case SystemMediaTransportControlsButton_Play:
                    if (impl->cb.onPlay)  impl->cb.onPlay();  break;
                case SystemMediaTransportControlsButton_Pause:
                    if (impl->cb.onPause) impl->cb.onPause(); break;
                case SystemMediaTransportControlsButton_Stop:
                    if (impl->cb.onStop)  impl->cb.onStop();  break;
                default: break;
            }
            return S_OK;
        });
    impl->smtc->add_ButtonPressed(btnHandler.Get(), &impl->tokenButton);

    // ── Seek / scrub handler (SMTC2 only) ────────────────────────────────────
    // Note: put_IsSeekEnabled does NOT exist on ISystemMediaTransportControls2
    // in SDK 19041.  Seeking is implicitly enabled by UpdateTimelineProperties.
    if (impl->smtc2 && impl->cb.onSeek) {
        auto seekHandler = Callback<SeekHandler>(
            [this](ISystemMediaTransportControls*,
                   IPlaybackPositionChangeRequestedEventArgs* args) -> HRESULT {
                ABI::Windows::Foundation::TimeSpan pos{};
                if (FAILED(args->get_RequestedPlaybackPosition(&pos))) return S_OK;
                // TimeSpan::Duration is in 100-nanosecond units → microseconds
                int64_t micros = pos.Duration / 10;
                impl->cb.onSeek(micros);
                return S_OK;
            });
        impl->smtc2->add_PlaybackPositionChangeRequested(
            seekHandler.Get(), &impl->tokenSeek);
        impl->seekRegistered = true;
    }

    // ── Display updater ───────────────────────────────────────────────────────
    // SDK 19041 ABI: property getter uses get_ prefix, not Get
    impl->smtc->get_DisplayUpdater(&impl->updater);
    if (impl->updater)
        impl->updater->put_Type(MediaPlaybackType_Music);

    return true;
}

void SmtcBridge::UpdatePlaybackState(bool isPlaying, bool isPaused, bool isFinished) {
    if (!impl || !impl->smtc) return;
    MediaPlaybackStatus status;
    if      (isFinished) status = MediaPlaybackStatus_Stopped;
    else if (isPaused)   status = MediaPlaybackStatus_Paused;
    else if (isPlaying)  status = MediaPlaybackStatus_Playing;
    else                 status = MediaPlaybackStatus_Closed;
    impl->smtc->put_PlaybackStatus(status);
}

void SmtcBridge::UpdatePosition(uint64_t currentMicros, uint64_t totalMicros) {
    if (!impl || !impl->smtc2) return;

    // RoActivateInstance returns IInspectable; QI to the timeline interface.
    ComPtr<IInspectable> raw;
    HRESULT hr = RoActivateInstance(
        HStringReference(
            RuntimeClass_Windows_Media_SystemMediaTransportControlsTimelineProperties).Get(),
        &raw);
    if (FAILED(hr)) return;

    ComPtr<ISystemMediaTransportControlsTimelineProperties> tl;
    if (FAILED(raw.As(&tl)) || !tl) return;

    // TimeSpan::Duration in 100-ns units; multiply micros by 10
    auto toTs = [](uint64_t us) -> ABI::Windows::Foundation::TimeSpan {
        return { static_cast<INT64>(us * 10) };
    };

    tl->put_StartTime(toTs(0));
    tl->put_EndTime(toTs(totalMicros));
    tl->put_Position(toTs(currentMicros));
    tl->put_MinSeekTime(toTs(0));
    tl->put_MaxSeekTime(toTs(totalMicros));
    impl->smtc2->UpdateTimelineProperties(tl.Get());
}

void SmtcBridge::UpdateMetadata(const std::string& title, const std::string& artist) {
    if (!impl || !impl->updater) return;

    ComPtr<IMusicDisplayProperties> music;
    impl->updater->get_MusicProperties(&music);
    if (!music) return;

    auto toHs = [](const std::string& s) {
        HString hs;
        hs.Set(std::wstring(s.begin(), s.end()).c_str());
        return hs;
    };

    auto ht = toHs(title);
    auto ha = toHs(artist);
    music->put_Title(ht.Get());
    music->put_Artist(ha.Get());
    impl->updater->Update();
}

void SmtcBridge::Shutdown() {
    if (!impl) return;
    if (impl->smtc) {
        impl->smtc->remove_ButtonPressed(impl->tokenButton);
        if (impl->smtc2 && impl->seekRegistered)
            impl->smtc2->remove_PlaybackPositionChangeRequested(impl->tokenSeek);
        impl->smtc->put_IsEnabled(false);
    }
    delete impl;
    impl = nullptr;
}

#endif // _WIN32