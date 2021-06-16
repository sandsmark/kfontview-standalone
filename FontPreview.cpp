/*
 * KFontInst - KDE Font Installer
 *
 * Copyright 2003-2007 Craig Drummond <craig@kde.org>
 *
 * ----
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "FontPreview.h"
#include "FcEngine.h"
#include "CharTip.h"
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QX11Info>
#include <QDebug>
#include <stdlib.h>

extern FT_Library qt_getFreetype();

namespace KFI {

static const int constBorder = 4;
static const int constStepSize = 16;

CFontPreview::CFontPreview(QWidget *parent)
    : QWidget(parent),
      itsCurrentFace(1),
      itsLastWidth(0),
      itsLastHeight(0),
      itsStyleInfo(KFI_NO_STYLE_INFO),
      itsTip(nullptr)
{
    m_library = nullptr;
    FT_Error err = FT_Init_FreeType(&m_library);
    if (err != 0) {
        qWarning() << "Failed to init freetype";
        m_library = nullptr;
    }

    itsEngine = new CFcEngine;
}

CFontPreview::~CFontPreview()
{
    delete itsTip;
    delete itsEngine;
    
    FT_Done_FreeType(m_library);
}

void CFontPreview::showFont(const QString &name, unsigned long styleInfo,
                            int face)
{
    itsFontName = name;
    itsStyleInfo = styleInfo;
    showFace(face);

    m_glyphRuns.clear();

    const QVector<int> sizes = getAvailableSizes(name);
    if (sizes.isEmpty()) {
        qWarning() << "Failed to get sizes";
        return;
    }
    m_rawFont = QRawFont(name, sizes.first());
    if (!m_rawFont.isValid()) {
        qWarning() << "Invalid font" << name;
        return;
    }
    m_previewString = previewString(name);
    if (m_previewString.isEmpty()) {
        qWarning() << "failed to create preview string";
        return;
    }
    for (int size :sizes) {
        m_glyphRuns.append(createGlyphRun(name, size, m_previewString));
    }
    m_family = m_rawFont.familyName();
}

QString CFontPreview::previewString(const QString &fontFile)
{

    QString ret;
    for (const QFontDatabase::WritingSystem writingSystem : m_rawFont.supportedWritingSystems()) {
        for (const QChar c : QFontDatabase::writingSystemSample(writingSystem)) {
            if (m_rawFont.supportsCharacter(c)) {
                ret += c;
            }
        }
    }
    if (!ret.isEmpty()) {
        return ret;
    }

    FT_Face face = 0;
    FT_Error err = FT_New_Face(m_library, QFile::encodeName(fontFile).constData(), 0, &face);
    if (err != 0) {
        qWarning() << "Failed to get face" << err;
        return {};
    }

    FT_ULong  charcode;
    FT_UInt   gindex;
    charcode = FT_Get_First_Char( face, &gindex );
    float currWidth = constBorder * 4;
    while ( gindex != 0 ) {
        const QRectF boundingRect = m_rawFont.boundingRect(gindex);
        currWidth += boundingRect.width();
        if (currWidth > width()) {
            break;
        }
        ret += QChar(uint(charcode));

        charcode = FT_Get_Next_Char( face, charcode, &gindex );
    }

    FT_Done_Face(face);
    return ret;
}

QList<QGlyphRun> CFontPreview::createGlyphRun(const QString &fontFile, const int size, const QString &text)
{
    QRawFont font(fontFile, size);
    if (!font.isValid()) {
        qWarning() << "Invalid font and size" << fontFile << size;
        return {};
    }

    QTextLayout layout(text);
    layout.beginLayout();
    layout.createLine();
    layout.endLayout();
    layout.setRawFont(font);
    QList<QGlyphRun> runs = layout.glyphRuns();
    if (runs.isEmpty()) {
        qWarning() << "No runs!";
    }
    if (runs.isEmpty()) {
        qWarning() << "Failed to create run for" << text;
        return runs;
    }

    if (runs.count() != 1) {
        qWarning() << "too many runs" << runs.count();
        runs = runs.mid(0, 1);
    }
    return runs;
}
QVector<int> CFontPreview::getAvailableSizes(const QString &filePath)
{
    const int constScalableSizes[] = {8, 10, 12, 24, 36, 48, 64, 72, 96, 0 };

    const int id = 0;
    FcBlanks *blanks = FcConfigGetBlanks(nullptr);
    int faces = 0;

    FcPattern *pattern = FcFreeTypeQuery((const FcChar8*)QFile::encodeName(filePath).constData(), id, blanks, &faces);
    if (!pattern) {
        qWarning() << "Failed to load" << filePath;
        return {};
    }
    FcBool scalable = FcTrue;
    FcResult res = FcPatternGetBool (pattern, FC_SCALABLE, 0, &scalable);

    if (res != FcResultMatch) {
        qWarning() << "Failed to query scalable";
        scalable = FcTrue;
    }
    FcPatternDestroy(pattern);
    QVector<int> sizes;
    if (scalable) {
        sizes.reserve(sizeof(constScalableSizes) / sizeof(int));

        for (int i = 0; constScalableSizes[i]; ++i) {
            sizes.push_back((constScalableSizes[i] * QX11Info::appDpiX() + 36) / 72);

            //if (px <= alphaSize) {
            //    itsAlphaSizeIndex = i;
            //}
        }

        return sizes;
    }
    if (!m_library) {
        return {};
    }

    FT_Face face = 0;
    FT_Error err = FT_New_Face(m_library, QFile::encodeName(filePath).constData(), 0, &face);
    if (err != 0) {
        qWarning() << "Failed to get face" << err;
        return {};
    }

    for (int i=0; i<face->num_fixed_sizes; i++) {
        sizes.push_back(face->available_sizes[i].y_ppem >> 6);
    }
    FT_Done_Face(face);

    return sizes;
}

void CFontPreview::showFace(int face)
{
    itsCurrentFace = face;
    showFont();
}

void CFontPreview::showFont()
{
    itsLastWidth = width() + constStepSize;
    itsLastHeight = height() + constStepSize;

    itsImage = itsEngine->draw(itsFontName, itsStyleInfo, itsCurrentFace,
                               palette().text().color(), palette().base().color(),
                               itsLastWidth, itsLastHeight,
                               false, itsRange, &itsChars);

    if (!itsImage.isNull()) {
        itsLastChar = CFcEngine::TChar();
        setMouseTracking(itsChars.count() > 0);
        update();
        emit status(true);
        emit atMax(itsEngine->atMax());
        emit atMin(itsEngine->atMin());
    } else {
        itsLastChar = CFcEngine::TChar();
        setMouseTracking(false);
        update();
        emit status(false);
        emit atMax(true);
        emit atMin(true);
    }
}

void CFontPreview::zoomIn()
{
    itsEngine->zoomIn();
    showFont();
    emit atMax(itsEngine->atMax());
}

void CFontPreview::zoomOut()
{
    itsEngine->zoomOut();
    showFont();
    emit atMin(itsEngine->atMin());
}

void CFontPreview::setUnicodeRange(const QList<CFcEngine::TRange> &r)
{
    itsRange = r;
    showFont();
}

void CFontPreview::paintEvent(QPaintEvent *)
{
    QPainter paint(this);

    paint.fillRect(rect(), palette().base());

    {
        paint.drawText(rect(), m_family);
        int offset = constBorder * 2;
        for (const QGlyphRun &run : m_glyphRuns) {
            // QPainter wants the baseline, so update the offset first
            offset += run.boundingRect().height() + constStepSize * 2;
            paint.drawGlyphRun({constBorder * 2, offset}, run);
        }
        return;
    }

    if (!itsImage.isNull()) {

        if (abs(width() - itsLastWidth) > constStepSize || abs(height() - itsLastHeight) > constStepSize) {
            showFont();
        } else {
            paint.drawImage(QPointF(constBorder, constBorder), itsImage,
                            QRectF(0, 0, (width() - (constBorder * 2)) * itsImage.devicePixelRatioF(),
                                   (height() - (constBorder * 2)) * itsImage.devicePixelRatioF()));
        }
    }
}

void CFontPreview::mouseMoveEvent(QMouseEvent *event)
{
    if (!itsChars.isEmpty()) {
        QList<CFcEngine::TChar>::ConstIterator end(itsChars.end());

        if (itsLastChar.isNull() || !itsLastChar.contains(event->pos())) {
            for (QList<CFcEngine::TChar>::ConstIterator it(itsChars.begin()); it != end; ++it) {
                if ((*it).contains(event->pos())) {
                    if (!itsTip) {
                        itsTip = new CCharTip(this);
                    }

                    itsTip->setItem(*it);
                    itsLastChar = *it;
                    break;
                }
            }
        }
    }
}

void CFontPreview::wheelEvent(QWheelEvent *e)
{
    if (e->angleDelta().y() > 0) {
        zoomIn();
    } else if (e->angleDelta().y() < 0) {
        zoomOut();
    }

    e->accept();
}

QSize CFontPreview::sizeHint() const
{
    return QSize(132, 132);
}

QSize CFontPreview::minimumSizeHint() const
{
    return QSize(32, 32);
}

}

