// album_art_patch.cpp
// Drop this into the Music-Integrations fork src/ folder and call
// fetchAlbumArt() whenever the overlay updates track info.
//
// Requires: Windows 10+ (winrt/Windows.Media.Control)
// Link:     windowsapp.lib (already linked if you use SMTC elsewhere)

#pragma once
#include <Geode/Geode.hpp>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Graphics.Imaging.h>

using namespace geode::prelude;
namespace WMC = winrt::Windows::Media::Control;
namespace Streams = winrt::Windows::Storage::Streams;

// ----------------------------------------------------------------
// 1. Grab raw JPEG/PNG bytes from the Windows Media Session thumbnail
// ----------------------------------------------------------------
std::vector<uint8_t> fetchThumbnailBytes() {
    try {
        auto sessions = WMC::GlobalSystemMediaTransportControlsSessionManager
            ::RequestAsync().get();
        auto session = sessions.GetCurrentSession();
        if (!session) return {};

        auto mediaProps = session.TryGetMediaPropertiesAsync().get();
        if (!mediaProps) return {};

        auto thumb = mediaProps.Thumbnail();
        if (!thumb) return {};

        auto stream = thumb.OpenReadAsync().get();
        uint64_t size = stream.Size();
        if (size == 0 || size > 1024 * 1024 * 4) return {}; // skip if >4MB

        Streams::Buffer buf(static_cast<uint32_t>(size));
        stream.ReadAsync(buf, static_cast<uint32_t>(size),
            Streams::InputStreamOptions::None).get();

        auto dataReader = Streams::DataReader::FromBuffer(buf);
        std::vector<uint8_t> bytes(size);
        dataReader.ReadBytes(bytes);
        return bytes;

    } catch (...) {
        return {};
    }
}

// ----------------------------------------------------------------
// 2. Convert raw image bytes → CCTexture2D (Cocos2d)
//    Works for JPEG and PNG thumbnails (YT Music sends JPEG)
// ----------------------------------------------------------------
CCTexture2D* bytesToTexture(const std::vector<uint8_t>& bytes) {
    if (bytes.empty()) return nullptr;

    auto* image = new CCImage();
    // CCImage::initWithImageData accepts raw JPEG/PNG bytes
    if (!image->initWithImageData(
            const_cast<void*>(static_cast<const void*>(bytes.data())),
            static_cast<int>(bytes.size()))) {
        image->release();
        return nullptr;
    }

    auto* texture = new CCTexture2D();
    if (!texture->initWithImage(image)) {
        image->release();
        texture->release();
        return nullptr;
    }

    image->release();
    texture->autorelease();
    return texture;
}

// ----------------------------------------------------------------
// 3. Helper: create or update a CCSprite on the overlay layer
//    Call this from your overlay's init() and from updateTrackInfo()
// ----------------------------------------------------------------
//
// Usage in your overlay class:
//
//   // In your overlay header, add:
//   CCSprite* m_albumArt = nullptr;
//
//   // Then call:
//   updateAlbumArt(this);  // 'this' is your overlay CCLayer
//
void updateAlbumArt(CCLayer* overlayLayer) {
    // Run thumbnail fetch on a background thread to avoid hitching
    std::thread([overlayLayer]() {
        auto bytes = fetchThumbnailBytes();

        // Schedule texture creation back on the main (GL) thread
        CCDirector::sharedDirector()->getScheduler()->scheduleSelector(
            schedule_selector([bytes, overlayLayer](float) {
                // Find or create the album art sprite
                // Tag 9999 is used to identify it — pick any free tag
                auto* existing = static_cast<CCSprite*>(
                    overlayLayer->getChildByTag(9999));

                auto* tex = bytesToTexture(bytes);

                if (!tex) {
                    // No art available — hide existing sprite if present
                    if (existing) existing->setVisible(false);
                    return;
                }

                if (existing) {
                    existing->setTexture(tex);
                    existing->setVisible(true);
                } else {
                    auto* sprite = CCSprite::createWithTexture(tex);

                    // ---- Position & size: adjust to fit your overlay layout ----
                    // Example: 64x64 thumbnail, top-left corner of overlay
                    sprite->setContentSize({ 64.f, 64.f });
                    sprite->setScaleX(64.f / tex->getContentSize().width);
                    sprite->setScaleY(64.f / tex->getContentSize().height);

                    // Anchor to top-left; offset from overlay origin
                    sprite->setAnchorPoint({ 0.f, 1.f });
                    // Adjust these to match where your title/artist labels sit
                    sprite->setPosition({ 8.f, overlayLayer->getContentSize().height - 8.f });

                    sprite->setTag(9999);
                    overlayLayer->addChild(sprite, 10);
                }
            }),
            overlayLayer,
            0.f,    // interval (0 = fire once next frame)
            0,      // repeat
            0.f,    // delay
            false
        );
    }).detach();
}

// ----------------------------------------------------------------
// INTEGRATION NOTES
// ----------------------------------------------------------------
//
// 1. In your overlay's init():
//      updateAlbumArt(this);
//
// 2. In your updateTrackInfo() (wherever you set title/artist labels):
//      updateAlbumArt(this);
//
// 3. In mod.json, no new dependencies needed — windowsapp.lib is
//    already available if you're using GlobalSystemMediaTransportControls.
//
// 4. YT Music specifically: it DOES populate the SMTC thumbnail.
//    If it ever returns empty, the sprite is hidden gracefully.
//
// 5. The thumbnail is typically 226x226 px JPEG from YT Music.
//    Adjust the 64.f size constant above to whatever fits your overlay.
