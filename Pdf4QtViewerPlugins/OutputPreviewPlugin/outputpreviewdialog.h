//    Copyright (C) 2021 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#ifndef OUTPUTPREVIEWDIALOG_H
#define OUTPUTPREVIEWDIALOG_H

#include "pdfdocument.h"
#include "pdfdrawwidget.h"
#include "pdftransparencyrenderer.h"

#include <QDialog>
#include <QFuture>
#include <QFutureWatcher>

namespace Ui
{
class OutputPreviewDialog;
}

namespace pdfplugin
{

class OutputPreviewDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OutputPreviewDialog(const pdf::PDFDocument* document, pdf::PDFWidget* widget, QWidget* parent);
    virtual ~OutputPreviewDialog() override;

    virtual void resizeEvent(QResizeEvent* event) override;
    virtual void closeEvent(QCloseEvent* event) override;
    virtual void showEvent(QShowEvent* event) override;

    virtual void accept() override;
    virtual void reject() override;

private:

    void updateInks();
    void updatePaperColorWidgets();

    void onPaperColorChanged();
    void onSimulateSeparationsChecked(bool checked);
    void onSimulatePaperColorChecked(bool checked);
    void onInksChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight, const QVector<int>& roles);

    struct RenderedImage
    {
        QImage image;
        QList<pdf::PDFRenderError> errors;
    };

    void updatePageImage();
    void onPageImageRendered();
    RenderedImage renderPage(const pdf::PDFPage* page, QSize renderSize, pdf::PDFRGB paperColor, uint32_t activeColorMask);
    bool isRenderingDone() const;

    Ui::OutputPreviewDialog* ui;
    pdf::PDFInkMapper m_inkMapper;
    pdf::PDFInkMapper m_inkMapperForRendering;
    const pdf::PDFDocument* m_document;
    pdf::PDFWidget* m_widget;
    bool m_needUpdateImage;

    QFuture<RenderedImage> m_future;
    QFutureWatcher<RenderedImage>* m_futureWatcher;
};

}   // namespace pdf

#endif // OUTPUTPREVIEWDIALOG_H