#ifndef __FONT_PREVIEW_H__
#define __FONT_PREVIEW_H__

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

#include <QImage>
#include <QSize>
#include <QWidget>
#include <QPaintEvent>
#include <QTextLayout>
#include <QRawFont>
#include "KfiConstants.h"

#include <ft2build.h>
#include <fontconfig/fontconfig.h>
#include FT_FREETYPE_H
#include <fontconfig/fcfreetype.h>

class QWheelEvent;

namespace KFI {

class CFontPreview : public QWidget
{
    Q_OBJECT

public:

    CFontPreview(QWidget *parent = nullptr);
    ~CFontPreview() override;

    void        paintEvent(QPaintEvent *) override;
    void        mouseMoveEvent(QMouseEvent *event) override;
    void        wheelEvent(QWheelEvent *e) override;
    QSize       sizeHint() const override;
    QSize       minimumSizeHint() const override;

    void        showFont(const QString &name,     // Thsi is either family name, or filename
                         unsigned long styleInfo = KFI_NO_STYLE_INFO, int face = 0);
    void        showFont();
protected:
    void resizeEvent(QResizeEvent*) override { m_previewString = previewString(itsFontName); update(); }

public Q_SLOTS:
    void        zoomIn();
    void        zoomOut();

Q_SIGNALS:

    void        status(bool);
    void        atMax(bool);
    void        atMin(bool);

private:
    QVector<int> getAvailableSizes(const QString &filename);
    QList<QGlyphRun> createGlyphRun(const QString &fontFile, const int size, const QString &text);
    QString previewString(const QString &fontFile);

    QImage itsImage;
    int itsCurrentFace,
        itsLastWidth,
        itsLastHeight,
        itsStyleInfo;
    QString itsFontName;

    QTextLayout m_layout;
    QRawFont m_rawFont;

    QList<QGlyphRun> m_previewRuns;
    QList<QGlyphRun> m_glyphRuns;
    QList<QGlyphRun> m_foxRuns;
    QString m_previewString;
    QString m_family;
    FT_Library m_library;

    friend class CCharTip;
};

}

#endif
