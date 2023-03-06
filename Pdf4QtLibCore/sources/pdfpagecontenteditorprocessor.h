//    Copyright (C) 2023 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT. If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFPAGECONTENTEDITORPROCESSOR_H
#define PDFPAGECONTENTEDITORPROCESSOR_H

#include "pdfpagecontentprocessor.h"

#include <QtXml>

namespace pdf
{

class PDF4QTLIBSHARED_EXPORT PDFEditedPageContent
{
public:
    PDFEditedPageContent() = default;
    PDFEditedPageContent(QDomDocument content);

    struct ContentTextInfo
    {
        PDFInteger id = 0;
        QRectF boundingRectangle;
        QDomElement textElement;
    };

    QDomNodeList getTextElements() const;
    std::vector<ContentTextInfo> getTextInfos() const;

    static QString getStringFromOperator(QDomElement operatorElement);
    static QString getOperatorToString(PDFPageContentProcessor::Operator operatorValue);
    static QString getOperandName(PDFPageContentProcessor::Operator operatorValue, int operandIndex);

private:
    QDomDocument m_content;
    QString m_contentAsString;
};

class PDF4QTLIBSHARED_EXPORT PDFPageContentEditorProcessor : public PDFPageContentProcessor
{
    using BaseClass = PDFPageContentProcessor;

public:
    PDFPageContentEditorProcessor(const PDFPage* page,
                                  const PDFDocument* document,
                                  const PDFFontCache* fontCache,
                                  const PDFCMS* CMS,
                                  const PDFOptionalContentActivity* optionalContentActivity,
                                  QTransform pagePointToDevicePointMatrix,
                                  const PDFMeshQualitySettings& meshQualitySettings);

    PDFEditedPageContent getEditedPageContent() const;

protected:
    virtual void performInterceptInstruction(Operator currentOperator, ProcessOrder processOrder, const QByteArray& operatorAsText) override;
    virtual void performPathPainting(const QPainterPath& path, bool stroke, bool fill, bool text, Qt::FillRule fillRule) override;
    virtual bool isContentKindSuppressed(ContentKind kind) const override;

private:
    QDomDocument m_document;
    QDomElement m_currentElement;
    QDomElement m_textElement;
    int m_contentElementId;
    QRectF m_boundingRect;
};

}   // namespace pdf

#endif // PDFPAGECONTENTEDITORPROCESSOR_H
