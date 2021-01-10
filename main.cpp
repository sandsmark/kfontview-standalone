#include <QApplication>
#include <QDebug>
#include <QFile>
#include "FontPreview.h"

int main(int argc, char *argv[])
{
    if (argc < 2) {
        qDebug() << "need a font";
        return 1;
    }

    if (!QFile::exists(argv[1])) {
        qWarning() << argv[1] << "does not exist";
        return 1;
    }

    QApplication app(argc, argv);

    KFI::CFontPreview preview;
    preview.showFont(argv[1]);

    if (argc > 2) {
        preview.engine()->setPreviewString(QString::fromLocal8Bit(argv[2]));
    }

    preview.show();

    app.exec();

    return 0;
}
