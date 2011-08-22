/*
 * Copyright 2010, The Android Open Source Project
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "BaseTileTexture.h"

#include "BaseTile.h"
#include "ClassTracker.h"
#include "DeleteTextureOperation.h"
#include "GLUtils.h"
#include "TilesManager.h"

#ifdef DEBUG

#include <cutils/log.h>
#include <wtf/text/CString.h>

#undef XLOG
#define XLOG(...) android_printLog(ANDROID_LOG_DEBUG, "BaseTileTexture", __VA_ARGS__)

#else

#undef XLOG
#define XLOG(...)

#endif // DEBUG

namespace WebCore {

BaseTileTexture::BaseTileTexture(uint32_t w, uint32_t h)
    : DoubleBufferedTexture(eglGetCurrentContext(),
                            TilesManager::instance()->getSharedTextureMode())
    , m_usedLevel(-1)
    , m_owner(0)
    , m_delayedReleaseOwner(0)
    , m_delayedRelease(false)
    , m_busy(false)
{
    m_size.set(w, h);
    m_ownTextureId = GLUtils::createBaseTileGLTexture(w, h);

    // Make sure they are created on the UI thread.
    TilesManager::instance()->transferQueue()->initSharedSurfaceTextures(w, h);

#ifdef DEBUG_COUNT
    ClassTracker::instance()->increment("BaseTileTexture");
#endif
}

BaseTileTexture::~BaseTileTexture()
{
    if (m_sharedTextureMode == EglImageMode) {
        SharedTexture* textures[3] = { m_textureA, m_textureB, 0 };
        destroyTextures(textures);
    }
#ifdef DEBUG_COUNT
    ClassTracker::instance()->decrement("BaseTileTexture");
#endif
}

void BaseTileTexture::destroyTextures(SharedTexture** textures)
{
    int x = 0;
    while (textures[x]) {
        // We need to delete the source texture and EGLImage in the texture
        // generation thread. In theory we should be able to delete the EGLImage
        // from either thread, but it currently throws an error if not deleted
        // in the same EGLContext from which it was created.
        textures[x]->lock();
        DeleteTextureOperation* operation = new DeleteTextureOperation(
            textures[x]->getSourceTextureId(), textures[x]->getEGLImage());
        textures[x]->unlock();
        TilesManager::instance()->scheduleOperation(operation);
        x++;
    }
}

TextureInfo* BaseTileTexture::producerLock()
{
    m_busyLock.lock();
    m_busy = true;
    m_busyLock.unlock();
    return DoubleBufferedTexture::producerLock();
}

void BaseTileTexture::producerRelease()
{
    DoubleBufferedTexture::producerRelease();
    setNotBusy();
}

void BaseTileTexture::producerReleaseAndSwap()
{
    DoubleBufferedTexture::producerReleaseAndSwap();
    setNotBusy();
}

void BaseTileTexture::setNotBusy()
{
    android::Mutex::Autolock lock(m_busyLock);
    m_busy = false;
    if (m_delayedRelease) {
        if (m_owner == m_delayedReleaseOwner)
            m_owner = 0;

        m_delayedRelease = false;
        m_delayedReleaseOwner = 0;
    }
    m_busyCond.signal();
}

bool BaseTileTexture::busy()
{
    android::Mutex::Autolock lock(m_busyLock);
    return m_busy;
}

void BaseTileTexture::producerUpdate(TextureInfo* textureInfo, const SkBitmap& bitmap)
{
    // no need to upload a texture since the bitmap is empty
    if (!bitmap.width() && !bitmap.height()) {
        producerRelease();
        return;
    }

    // After the tiled layer checked in, this is not called anyway.
    // TODO: cleanup the old code path for layer painting
    // GLUtils::paintTextureWithBitmap(info, m_size, bitmap, 0, 0);

    producerReleaseAndSwap();
}

bool BaseTileTexture::acquire(TextureOwner* owner, bool force)
{
    if (m_owner == owner) {
        if (m_delayedRelease) {
            m_delayedRelease = false;
            m_delayedReleaseOwner = 0;
        }
        return true;
    }

    return setOwner(owner, force);
}

bool BaseTileTexture::tryAcquire(TextureOwner* owner)
{
    m_busyLock.lock();
    if (!m_busy
        && m_owner
        && m_owner->state() != owner->state()) {
        m_busyLock.unlock();
        return this->acquire(owner);
    }
    m_busyLock.unlock();
    return false;
}

bool BaseTileTexture::setOwner(TextureOwner* owner, bool force)
{
    // if the writable texture is busy (i.e. currently being written to) then we
    // can't change the owner out from underneath that texture
    m_busyLock.lock();
    while (m_busy && force)
        m_busyCond.wait(m_busyLock);
    bool busy = m_busy;
    m_busyLock.unlock();

    if (!busy) {
        // if we are not busy we can try to remove the texture from the layer;
        // LayerAndroid::removeTexture() is protected by the same lock as
        // LayerAndroid::paintBitmapGL(), so either we execute removeTexture()
        // first and paintBitmapGL() will bail out, or we execute it after,
        // and paintBitmapGL() will mark the texture as busy before
        // relinquishing the lock. LayerAndroid::removeTexture() will call
        // BaseTileTexture::release(), which will then do nothing
        // if the texture is busy and we then don't return true.
        bool proceed = true;
        if (m_owner && m_owner != owner)
            proceed = m_owner->removeTexture(this);

        if (proceed) {
            m_owner = owner;
            return true;
        }
    }
    return false;
}

bool BaseTileTexture::release(TextureOwner* owner)
{
    android::Mutex::Autolock lock(m_busyLock);
    if (m_owner != owner)
        return false;

    if (!m_busy) {
        m_owner = 0;
    } else {
        m_delayedRelease = true;
        m_delayedReleaseOwner = owner;
    }
    return true;
}

void BaseTileTexture::setTile(TextureInfo* info, int x, int y,
                                          float scale, TilePainter* painter,
                                          unsigned int pictureCount)
{
    TextureTileInfo* textureInfo = m_texturesInfo.get(getWriteableTexture());
    if (!textureInfo) {
        textureInfo = new TextureTileInfo();
    }
    textureInfo->m_x = x;
    textureInfo->m_y = y;
    textureInfo->m_scale = scale;
    textureInfo->m_painter = painter;
    textureInfo->m_picture = pictureCount;
    m_texturesInfo.set(getWriteableTexture(), textureInfo);
}

float BaseTileTexture::scale()
{
    TextureTileInfo* textureInfo = &m_ownTextureTileInfo;
    return textureInfo->m_scale;
}

// This function + TilesManager::addItemInTransferQueue() is replacing the
// setTile().
void BaseTileTexture::setOwnTextureTileInfoFromQueue(const TextureTileInfo* info)
{
    m_ownTextureTileInfo.m_x = info->m_x;
    m_ownTextureTileInfo.m_y = info->m_y;
    m_ownTextureTileInfo.m_scale = info->m_scale;
    m_ownTextureTileInfo.m_painter = info->m_painter;
    m_ownTextureTileInfo.m_picture = info->m_picture;
    m_ownTextureTileInfo.m_inverted = TilesManager::instance()->invertedScreen();
}

bool BaseTileTexture::readyFor(BaseTile* baseTile)
{
    const TextureTileInfo* info = &m_ownTextureTileInfo;
    if (info &&
        (info->m_x == baseTile->x()) &&
        (info->m_y == baseTile->y()) &&
        (info->m_scale == baseTile->scale()) &&
        (info->m_painter == baseTile->painter()) &&
        (info->m_inverted == TilesManager::instance()->invertedScreen()))
        return true;

    XLOG("readyFor return false for tile x, y (%d %d) texId %d ,"
         " BaseTileTexture %p, BaseTile is %p",
         baseTile->x(), baseTile->y(), m_ownTextureId, this, baseTile);

    return false;
}

} // namespace WebCore