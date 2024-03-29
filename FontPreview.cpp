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
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QX11Info>
#include <QDebug>
#include <QScreen>
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
      itsStyleInfo(KFI_NO_STYLE_INFO)
{
    m_library = nullptr;
    FT_Error err = FT_Init_FreeType(&m_library);
    if (err != 0) {
        qWarning() << "Failed to init freetype";
        m_library = nullptr;
    }
}

CFontPreview::~CFontPreview()
{
    FT_Done_FreeType(m_library);
}

void CFontPreview::showFont(const QString &name, unsigned long styleInfo,
                            int face)
{
    itsFontName = name;
    itsStyleInfo = styleInfo;

    m_glyphRuns.clear();
    m_previewRuns.clear();
    m_foxRuns.clear();

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

    static const QString quickBrownFox =
        i18nc("A sentence that uses all of the letters of the alphabet",
                "The quick brown fox jumps over the lazy dog");
    for (int size :sizes) {
        m_foxRuns.append(createGlyphRun(name, size, quickBrownFox));
    }

    static const QStringList previewStrings = {
        i18nc("All of the letters of the alphabet, uppercase", "ABCDEFGHIJKLMNOPQRSTUVWXYZ"),
        i18nc("All of the letters of the alphabet, lowercase", "abcdefghijklmnopqrstuvwxyz"),
        //i18nc("Numbers and characters", "0123456789.:,;(*!?'/\\\")£$€%^&-+@~#<>{}[]") //krazy:exclude=i18ncheckarg
        QString::fromUtf8("0123456789.:,;(*!?'/\\\")£$€%^&-+@~#<>{}[]") //krazy:exclude=i18ncheckarg
    };

    int defaultSize = fontInfo().pixelSize();
    if (defaultSize == -1) {
        int pointSize = fontInfo().pointSize();
        defaultSize = pointSize / (72 * screen()->physicalDotsPerInch());
    }
    for (const QString &previewString : previewStrings) {
        qDebug() << previewString;
        m_previewRuns.append(createGlyphRun(name, defaultSize, previewString));
    }


    m_family = m_rawFont.familyName() + ", " + m_rawFont.styleName();
}

QString CFontPreview::previewString(const QString &fontFile)
{

    QString ret;
    for (const QFontDatabase::WritingSystem writingSystem : m_rawFont.supportedWritingSystems()) {
        qDebug() << "Checking writing system" << writingSystem;
        for (const QChar c : QFontDatabase::writingSystemSample(writingSystem)) {
            if (ret.contains(c)) {
                continue;
            }
            if (m_rawFont.supportsCharacter(c)) {
                ret += c;
            }
        }
    }
    qDebug() << ret;
    if (!ret.isEmpty()) {
        return ret;
    }

    FT_Face face = 0;
    FT_Error err = FT_New_Face(m_library, QFile::encodeName(fontFile).constData(), 0, &face);
    if (err != 0) {
        qWarning() << "Failed to get face" << err;
        return {};
    }

    FT_ULong charcode;
    FT_UInt gindex;
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
        qWarning() << "too many runs" << runs.count() << "assuming it did some replacements for us";
        QList<QGlyphRun> valid;
        for (const QGlyphRun &run : runs) {
            if (run.rawFont() != font) {
                continue;
            }
            valid.append(run);
        }
        return valid;
    }
    return runs;
}
QVector<int> CFontPreview::getAvailableSizes(const QString &filePath)
{
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
        return {8, 10, 12, 24, 36, 48, 64, 72, 96};
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

void CFontPreview::paintEvent(QPaintEvent *)
{
    QPainter paint(this);

    paint.fillRect(rect(), palette().base());

    QRect textRect = rect();
    textRect -= QMargins(constBorder, constBorder, constBorder, constBorder);
    QRect bounds;
    paint.drawText(textRect, 0, m_family, &bounds);

    qreal offset = bounds.height() + constBorder;

    paint.drawLine(constBorder, offset, width() - constBorder, offset);
    offset += constBorder;

    for (const QGlyphRun &run : m_previewRuns) {
        // QPainter wants the baseline, so update the offset first
        paint.drawGlyphRun({constBorder * 2, offset}, run);
        offset += run.boundingRect().height() + constBorder;
    }
    offset += constBorder;

    paint.drawLine(constBorder, offset, width() - constBorder, offset);
    offset += constBorder;

    int lastHeight = 0;
    for (const QGlyphRun &run : m_glyphRuns) {
        lastHeight = run.boundingRect().height();
        offset += lastHeight;
        paint.drawGlyphRun({constBorder * 2, offset}, run);

    }

    offset += constBorder + lastHeight/2;
    paint.drawLine(constBorder, offset, width() - constBorder, offset);
    offset += constBorder;

    for (const QGlyphRun &run : m_foxRuns) {
        offset += run.boundingRect().height();
        paint.drawGlyphRun({constBorder * 2, offset}, run);
    }
    return;
}

void CFontPreview::mouseMoveEvent(QMouseEvent *event)
{
    //if (!itsChars.isEmpty()) {
    //    QList<CFcEngine::TChar>::ConstIterator end(itsChars.end());

    //    if (itsLastChar.isNull() || !itsLastChar.contains(event->pos())) {
    //        for (QList<CFcEngine::TChar>::ConstIterator it(itsChars.begin()); it != end; ++it) {
    //            if ((*it).contains(event->pos())) {
    //                if (!itsTip) {
    //                    itsTip = new CCharTip(this);
    //                }

    //                itsTip->setItem(*it);
    //                itsLastChar = *it;
    //                break;
    //            }
    //        }
    //    }
    //}
}
void CFontPreview::zoomIn()
{
    // todo
}
void CFontPreview::zoomOut()
{
    // todo
}

void CFontPreview::wheelEvent(QWheelEvent *e)
{
    //if (e->angleDelta().y() > 0) {
    //    zoomIn();
    //} else if (e->angleDelta().y() < 0) {
    //    zoomOut();
    //}

    //e->accept();
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

