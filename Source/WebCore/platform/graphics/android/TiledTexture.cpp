/*
 * Copyright 2011, The Android Open Source Project
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
#include "TiledTexture.h"

#include "TilesManager.h"
#include "TilesTracker.h"

#include "PaintedSurface.h"
#include "PaintTileOperation.h"
#include "SkCanvas.h"

#ifdef DEBUG

#include <cutils/log.h>
#include <wtf/CurrentTime.h>
#include <wtf/text/CString.h>

#undef XLOG
#define XLOG(...) android_printLog(ANDROID_LOG_DEBUG, "TiledTexture", __VA_ARGS__)

#else

#undef XLOG
#define XLOG(...)

#endif // DEBUG

namespace WebCore {

void TiledTexture::prepare(GLWebViewState* state, bool repaint)
{
    if (!m_surface)
        return;

    if (!m_surface->layer())
        return;

    // first, how many tiles do we need
    IntRect visibleArea = m_surface->visibleArea();
    IntRect area(visibleArea.x() * m_surface->scale(),
                 visibleArea.y() * m_surface->scale(),
                 ceilf(visibleArea.width() * m_surface->scale()),
                 ceilf(visibleArea.height() * m_surface->scale()));

    for (unsigned int i = 0; i < m_tiles.size(); i++) {
        BaseTile* tile = m_tiles[i];
        tile->setUsedLevel(-1);
    }

    if (area.width() == 0 && area.height() == 0) {
        m_area.setWidth(0);
        m_area.setHeight(0);
        return;
    }

    int tileWidth = TilesManager::instance()->layerTileWidth();
    int tileHeight = TilesManager::instance()->layerTileHeight();

    m_area.setX(area.x() / tileWidth);
    m_area.setY(area.y() / tileHeight);
    float right = (area.x() + area.width()) / (float) tileWidth;
    float bottom = (area.y() + area.height()) / (float) tileHeight;
    m_area.setWidth(ceilf(right) - m_area.x());
    m_area.setHeight(ceilf(bottom) - m_area.y());

    XLOG("for TiledTexture %x, we have a visible area of %d, %d - %d x %d, corresponding to %d, %d x - %d x %d tiles",
         this,
         visibleArea.x(), visibleArea.y(),
         visibleArea.width(), visibleArea.height(),
         m_area.x(), m_area.y(),
         m_area.width(), m_area.height());

    bool goingDown = m_prevTileY < m_area.y();
    m_prevTileY = m_area.y();

    if (m_surface->scale() != m_prevScale)
        TilesManager::instance()->removeOperationsForFilter(new ScaleFilter(m_surface->scale()));

    m_prevScale = m_surface->scale();

    for (int i = 0; i < m_area.width(); i++) {
        if (goingDown) {
            for (int j = 0; j < m_area.height(); j++) {
                prepareTile(repaint, m_area.x() + i, m_area.y() + j);
            }
        } else {
            for (int j = m_area.height() - 1; j >= 0; j--) {
                prepareTile(repaint, m_area.x() + i, m_area.y() + j);
            }
        }
    }
}

void TiledTexture::prepareTile(bool repaint, int x, int y)
{
    BaseTile* tile = getTile(x, y);
    if (!tile) {
        tile = new BaseTile(true);
        m_tiles.append(tile);
    }
    tile->reserveTexture();
    if (!tile->texture())
        return;

    tile->setContents(this, x, y, m_surface->scale());
    tile->setUsedLevel(0);

    bool schedule = false;
    if (repaint)
        tile->fullInval();
    if (!tile->isTileReady())
        schedule = true;
    if (repaint || tile->isDirty())
        schedule = true;

    LayerAndroid* layer = m_surface->layer();
    if (schedule && layer && !tile->isRepaintPending()) {
        PaintTileOperation *operation = new PaintTileOperation(tile, m_surface);
        TilesManager::instance()->scheduleOperation(operation);
    }
}

BaseTile* TiledTexture::getTile(int x, int y)
{
    for (unsigned int i = 0; i <m_tiles.size(); i++) {
        BaseTile* tile = m_tiles[i];
        if (tile->x() == x && tile->y() == y)
            return tile;
    }
    return 0;
}

bool TiledTexture::draw()
{
#ifdef DEBUG
    TilesManager::instance()->getTilesTracker()->trackLayer();
#endif

    bool askRedraw = false;
    if (m_area.width() == 0 || m_area.height() == 0)
        return askRedraw;

#ifdef DEBUG
    TilesManager::instance()->getTilesTracker()->trackVisibleLayer();
#endif

    float m_invScale = 1 / m_surface->scale();
    const float tileWidth = TilesManager::layerTileWidth() * m_invScale;
    const float tileHeight = TilesManager::layerTileHeight() * m_invScale;
    XLOG("draw tile %x, tiles %d", this, m_tiles.size());
    for (unsigned int i = 0; i <m_tiles.size(); i++) {
        BaseTile* tile = m_tiles[i];
        if (tile->x() >= m_area.x()
            && tile->x() < m_area.x() + m_area.width()
            && tile->y() >= m_area.y()
            && tile->y() < m_area.y() + m_area.height()) {
            SkRect rect;
            rect.fLeft = tile->x() * tileWidth;
            rect.fTop = tile->y() * tileHeight;
            rect.fRight = rect.fLeft + tileWidth;
            rect.fBottom = rect.fTop + tileHeight;
            XLOG(" - [%d], { painter %x vs %x }, tile %x %d,%d at scale %.2f [ready: %d]", i, this, tile->painter(), tile, tile->x(), tile->y(), tile->scale(), tile->isTileReady());
            askRedraw |= !tile->isTileReady();
            tile->draw(m_surface->opacity(), rect, m_surface->scale());
#ifdef DEBUG
            TilesManager::instance()->getTilesTracker()->track(tile->isTileReady(), tile->texture());
#endif
        }
    }
    return askRedraw;
}

bool TiledTexture::paint(BaseTile* tile, SkCanvas* canvas, unsigned int* pictureUsed)
{
    if (!m_surface)
        return false;

    XLOG("painting scheduled tile(%x : %d, %d, %.2f, %x) for %x",
         tile, tile->x(), tile->y(), tile->scale(), tile->painter(), this);
    return m_surface->paint(tile, canvas, pictureUsed);
}

void TiledTexture::paintExtra(SkCanvas* canvas)
{
    m_surface->paintExtra(canvas);
}

const TransformationMatrix* TiledTexture::transform()
{
    return m_surface->transform();
}

void TiledTexture::beginPaint()
{
    if (m_surface)
        m_surface->beginPaint();
}

void TiledTexture::endPaint()
{
    if (m_surface)
        m_surface->endPaint();
}

void TiledTexture::removeTiles()
{
    for (unsigned int i = 0; i < m_tiles.size(); i++) {
        delete m_tiles[i];
    }
}

bool TiledTexture::owns(BaseTileTexture* texture)
{
    for (unsigned int i = 0; i < m_tiles.size(); i++) {
        BaseTile* tile = m_tiles[i];
        if (tile->texture() == texture)
            return true;
    }
    return false;
}

} // namespace WebCore