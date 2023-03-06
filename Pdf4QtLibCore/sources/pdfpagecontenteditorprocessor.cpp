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

#include "pdfpagecontenteditorprocessor.h"

namespace pdf
{

PDFPageContentEditorProcessor::PDFPageContentEditorProcessor(const PDFPage* page,
                                                             const PDFDocument* document,
                                                             const PDFFontCache* fontCache,
                                                             const PDFCMS* CMS,
                                                             const PDFOptionalContentActivity* optionalContentActivity,
                                                             QTransform pagePointToDevicePointMatrix,
                                                             const PDFMeshQualitySettings& meshQualitySettings) :
    BaseClass(page, document, fontCache, CMS, optionalContentActivity, pagePointToDevicePointMatrix, meshQualitySettings),
    m_contentElementId(0)
{
    m_document = QDomDocument();

    QDomProcessingInstruction processingInstruction = m_document.createProcessingInstruction("xml", "version=\"1.0\" encoding=\"utf-8\"");
    m_document.appendChild(processingInstruction);

    QDomElement element = m_document.createElement("content");
    m_document.appendChild(element);

    m_currentElement = m_document.documentElement();
}

PDFEditedPageContent PDFPageContentEditorProcessor::getEditedPageContent() const
{
    return PDFEditedPageContent(m_document);
}

void PDFPageContentEditorProcessor::performInterceptInstruction(Operator currentOperator,
                                                                ProcessOrder processOrder,
                                                                const QByteArray& operatorAsText)
{
    if (processOrder == ProcessOrder::BeforeOperation)
    {
        if (currentOperator == Operator::TextBegin && !isTextProcessing())
        {
            m_textElement = m_document.createElement("text");
            m_textElement.setAttribute("id", ++m_contentElementId);
            m_document.documentElement().appendChild(m_textElement);
            m_currentElement = m_textElement;
            m_boundingRect = QRectF();
        }

        QDomElement instructionElement = m_document.createElement("op");
        m_currentElement.appendChild(instructionElement);
        instructionElement.setAttribute("type", QString::fromLatin1(operatorAsText));
        instructionElement.setAttribute("code", int(currentOperator));

        const auto& operands = getOperands();
        if (!operands.empty())
        {
            const size_t operandCount = operands.size();
            for (size_t i = 0; i < operandCount; ++i)
            {
                const PDFLexicalAnalyzer::Token& token = operands[i];
                QDomElement operandElement = m_document.createElement("par");
                instructionElement.appendChild(operandElement);
                operandElement.setAttribute("type", int(token.type));
                operandElement.setAttribute("value", token.data.toString());
            }
        }
    }
    else
    {
        if (currentOperator == Operator::TextEnd && !isTextProcessing())
        {
            m_currentElement = m_document.documentElement();

            if (!m_boundingRect.isEmpty())
            {
                QDomElement boundingBoxElement = m_document.createElement("bb");
                m_textElement.appendChild(boundingBoxElement);

                boundingBoxElement.setAttribute("x", QString::number(m_boundingRect.x()));
                boundingBoxElement.setAttribute("y", QString::number(m_boundingRect.y()));
                boundingBoxElement.setAttribute("width", QString::number(m_boundingRect.width()));
                boundingBoxElement.setAttribute("height", QString::number(m_boundingRect.height()));
            }

            m_textElement = QDomElement();
            m_boundingRect = QRectF();
        }
    }
}

void PDFPageContentEditorProcessor::performPathPainting(const QPainterPath& path, bool stroke, bool fill, bool text, Qt::FillRule fillRule)
{
    if (path.isEmpty())
    {
        return;
    }

    Q_UNUSED(fillRule);
    Q_UNUSED(stroke);
    Q_UNUSED(fill);
    Q_UNUSED(text);

    QPainterPath mappedPath = getCurrentWorldMatrix().map(path);
    QRectF boundingRect = mappedPath.boundingRect();
    m_boundingRect = m_boundingRect.united(boundingRect);
}

bool PDFPageContentEditorProcessor::isContentKindSuppressed(ContentKind kind) const
{
    switch (kind)
    {
        case ContentKind::Images:
        case ContentKind::Forms:
        case ContentKind::Shading:
        case ContentKind::Tiling:
            return true;

        default:
            break;
    }

    return false;
}

PDFEditedPageContent::PDFEditedPageContent(QDomDocument content) :
    m_content(std::move(content))
{
    m_contentAsString = m_content.toString(2);
}

QDomNodeList PDFEditedPageContent::getTextElements() const
{
    return m_content.elementsByTagName("text");
}

std::vector<PDFEditedPageContent::ContentTextInfo> PDFEditedPageContent::getTextInfos() const
{
    std::vector<PDFEditedPageContent::ContentTextInfo> result;

    QDomNodeList textElements = getTextElements();
    for (int i = 0; i < textElements.size(); ++i)
    {
        ContentTextInfo info;
        QDomElement textElement = textElements.at(i).toElement();
        QDomElement boundingBoxElement = textElement.firstChildElement("bb");

        info.id = textElement.attribute("id").toInt();
        info.textElement = textElement;

        if (!boundingBoxElement.isNull())
        {
            qreal x = boundingBoxElement.attribute("x").toDouble();
            qreal y = boundingBoxElement.attribute("y").toDouble();
            qreal w = boundingBoxElement.attribute("width").toDouble();
            qreal h = boundingBoxElement.attribute("height").toDouble();

            info.boundingRectangle = QRectF(x, y, w, h);
        }

        result.push_back(info);
    }

    return result;
}

QString PDFEditedPageContent::getStringFromOperator(QDomElement operatorElement)
{
    QString operatorString = operatorElement.attribute("code");
    PDFPageContentProcessor::Operator operand = static_cast<PDFPageContentProcessor::Operator>(operatorString.toInt());

    auto getOperand = [](int index) -> QString
    {

    };

    switch (operand)
    {
        case pdf::PDFPageContentProcessor::Operator::SetLineWidth:
            break;

        case pdf::PDFPageContentProcessor::Operator::SetLineCap:
            break;
        case pdf::PDFPageContentProcessor::Operator::SetLineJoin:
            break;
        case pdf::PDFPageContentProcessor::Operator::SetMitterLimit:
            break;
        case pdf::PDFPageContentProcessor::Operator::SetLineDashPattern:
            break;
        case pdf::PDFPageContentProcessor::Operator::SetRenderingIntent:
            break;
        case pdf::PDFPageContentProcessor::Operator::SetFlatness:
            break;
        case pdf::PDFPageContentProcessor::Operator::SetGraphicState:
            break;
        case pdf::PDFPageContentProcessor::Operator::SaveGraphicState:
            break;
        case pdf::PDFPageContentProcessor::Operator::RestoreGraphicState:
            break;
        case pdf::PDFPageContentProcessor::Operator::AdjustCurrentTransformationMatrix:
            break;
        case pdf::PDFPageContentProcessor::Operator::MoveCurrentPoint:
            break;
        case pdf::PDFPageContentProcessor::Operator::LineTo:
            break;
        case pdf::PDFPageContentProcessor::Operator::Bezier123To:
            break;
        case pdf::PDFPageContentProcessor::Operator::Bezier23To:
            break;
        case pdf::PDFPageContentProcessor::Operator::Bezier13To:
            break;
        case pdf::PDFPageContentProcessor::Operator::EndSubpath:
            break;
        case pdf::PDFPageContentProcessor::Operator::Rectangle:
            break;
        case pdf::PDFPageContentProcessor::Operator::PathStroke:
            break;
        case pdf::PDFPageContentProcessor::Operator::PathCloseStroke:
            break;
        case pdf::PDFPageContentProcessor::Operator::PathFillWinding:
            break;
        case pdf::PDFPageContentProcessor::Operator::PathFillWinding2:
            break;
        case pdf::PDFPageContentProcessor::Operator::PathFillEvenOdd:
            break;
        case pdf::PDFPageContentProcessor::Operator::PathFillStrokeWinding:
            break;
        case pdf::PDFPageContentProcessor::Operator::PathFillStrokeEvenOdd:
            break;
        case pdf::PDFPageContentProcessor::Operator::PathCloseFillStrokeWinding:
            break;
        case pdf::PDFPageContentProcessor::Operator::PathCloseFillStrokeEvenOdd:
            break;
        case pdf::PDFPageContentProcessor::Operator::PathClear:
            break;
        case pdf::PDFPageContentProcessor::Operator::ClipWinding:
            break;
        case pdf::PDFPageContentProcessor::Operator::ClipEvenOdd:
            break;
        case pdf::PDFPageContentProcessor::Operator::TextBegin:
            break;
        case pdf::PDFPageContentProcessor::Operator::TextEnd:
            break;
        case pdf::PDFPageContentProcessor::Operator::TextSetCharacterSpacing:
            break;
        case pdf::PDFPageContentProcessor::Operator::TextSetWordSpacing:
            break;
        case pdf::PDFPageContentProcessor::Operator::TextSetHorizontalScale:
            break;
        case pdf::PDFPageContentProcessor::Operator::TextSetLeading:
            break;
        case pdf::PDFPageContentProcessor::Operator::TextSetFontAndFontSize:
            break;
        case pdf::PDFPageContentProcessor::Operator::TextSetRenderMode:
            break;
        case pdf::PDFPageContentProcessor::Operator::TextSetRise:
            break;
        case pdf::PDFPageContentProcessor::Operator::TextMoveByOffset:
            break;
        case pdf::PDFPageContentProcessor::Operator::TextSetLeadingAndMoveByOffset:
            break;
        case pdf::PDFPageContentProcessor::Operator::TextSetMatrix:
            break;
        case pdf::PDFPageContentProcessor::Operator::TextMoveByLeading:
            break;
        case pdf::PDFPageContentProcessor::Operator::TextShowTextString:
            break;
        case pdf::PDFPageContentProcessor::Operator::TextShowTextIndividualSpacing:
            break;
        case pdf::PDFPageContentProcessor::Operator::TextNextLineShowText:
            break;
        case pdf::PDFPageContentProcessor::Operator::TextSetSpacingAndShowText:
            break;
        case pdf::PDFPageContentProcessor::Operator::Type3FontSetOffset:
            break;
        case pdf::PDFPageContentProcessor::Operator::Type3FontSetOffsetAndBB:
            break;
        case pdf::PDFPageContentProcessor::Operator::ColorSetStrokingColorSpace:
            break;
        case pdf::PDFPageContentProcessor::Operator::ColorSetFillingColorSpace:
            break;
        case pdf::PDFPageContentProcessor::Operator::ColorSetStrokingColor:
            break;
        case pdf::PDFPageContentProcessor::Operator::ColorSetStrokingColorN:
            break;
        case pdf::PDFPageContentProcessor::Operator::ColorSetFillingColor:
            break;
        case pdf::PDFPageContentProcessor::Operator::ColorSetFillingColorN:
            break;
        case pdf::PDFPageContentProcessor::Operator::ColorSetDeviceGrayStroking:
            break;
        case pdf::PDFPageContentProcessor::Operator::ColorSetDeviceGrayFilling:
            break;
        case pdf::PDFPageContentProcessor::Operator::ColorSetDeviceRGBStroking:
            break;
        case pdf::PDFPageContentProcessor::Operator::ColorSetDeviceRGBFilling:
            break;
        case pdf::PDFPageContentProcessor::Operator::ColorSetDeviceCMYKStroking:
            break;
        case pdf::PDFPageContentProcessor::Operator::ColorSetDeviceCMYKFilling:
            break;
        case pdf::PDFPageContentProcessor::Operator::ShadingPaintShape:
            break;
        case pdf::PDFPageContentProcessor::Operator::InlineImageBegin:
            break;
        case pdf::PDFPageContentProcessor::Operator::InlineImageData:
            break;
        case pdf::PDFPageContentProcessor::Operator::InlineImageEnd:
            break;
        case pdf::PDFPageContentProcessor::Operator::PaintXObject:
            break;
        case pdf::PDFPageContentProcessor::Operator::MarkedContentPoint:
            break;
        case pdf::PDFPageContentProcessor::Operator::MarkedContentPointWithProperties:
            break;
        case pdf::PDFPageContentProcessor::Operator::MarkedContentBegin:
            break;
        case pdf::PDFPageContentProcessor::Operator::MarkedContentBeginWithProperties:
            break;
        case pdf::PDFPageContentProcessor::Operator::MarkedContentEnd:
            break;
        case pdf::PDFPageContentProcessor::Operator::CompatibilityBegin:
            break;
        case pdf::PDFPageContentProcessor::Operator::CompatibilityEnd:
            break;

        default:
            break;
    }

    return QString();
}

QString PDFEditedPageContent::getOperatorToString(PDFPageContentProcessor::Operator operatorValue)
{
    switch (operatorValue)
    {
        case pdf::PDFPageContentProcessor::Operator::SetLineWidth:
            return "set_line_width";
        case pdf::PDFPageContentProcessor::Operator::SetLineCap:
            return "set_line_cap";
        case pdf::PDFPageContentProcessor::Operator::SetLineJoin:
            return "set_line_join";
        case pdf::PDFPageContentProcessor::Operator::SetMitterLimit:
            return "set_mitter_limit";
        case pdf::PDFPageContentProcessor::Operator::SetLineDashPattern:
            return "set_line_dash_pattern";
        case pdf::PDFPageContentProcessor::Operator::SetRenderingIntent:
            return "set_rendering_intent";
        case pdf::PDFPageContentProcessor::Operator::SetFlatness:
            return "set_flatness";
        case pdf::PDFPageContentProcessor::Operator::SetGraphicState:
            return "set_graphic_state";
        case pdf::PDFPageContentProcessor::Operator::SaveGraphicState:
            return "save";
        case pdf::PDFPageContentProcessor::Operator::RestoreGraphicState:
            return "restore";
        case pdf::PDFPageContentProcessor::Operator::AdjustCurrentTransformationMatrix:
            return "set_cm";
        case pdf::PDFPageContentProcessor::Operator::MoveCurrentPoint:
            return "move_to";
        case pdf::PDFPageContentProcessor::Operator::LineTo:
            return "line_to";
        case pdf::PDFPageContentProcessor::Operator::Bezier123To:
            return "cubic123_to";
        case pdf::PDFPageContentProcessor::Operator::Bezier23To:
            return "cubic23_to";
        case pdf::PDFPageContentProcessor::Operator::Bezier13To:
            return "cubic13_to";
        case pdf::PDFPageContentProcessor::Operator::EndSubpath:
            return "close_path";
        case pdf::PDFPageContentProcessor::Operator::Rectangle:
            return "rect";
        case pdf::PDFPageContentProcessor::Operator::PathStroke:
            return "path_stroke";
        case pdf::PDFPageContentProcessor::Operator::PathCloseStroke:
            return "path_close_and_stroke";
        case pdf::PDFPageContentProcessor::Operator::PathFillWinding:
            return "path_fill_winding";
        case pdf::PDFPageContentProcessor::Operator::PathFillWinding2:
            return "path_fill_winding";
        case pdf::PDFPageContentProcessor::Operator::PathFillEvenOdd:
            return "path_fill_even_odd";
        case pdf::PDFPageContentProcessor::Operator::PathFillStrokeWinding:
            return "path_fill_stroke_winding";
        case pdf::PDFPageContentProcessor::Operator::PathFillStrokeEvenOdd:
            return "path_fill_stroke_even_odd";
        case pdf::PDFPageContentProcessor::Operator::PathCloseFillStrokeWinding:
            return "path_close_fill_stroke_winding";
        case pdf::PDFPageContentProcessor::Operator::PathCloseFillStrokeEvenOdd:
            return "path_close_fill_stroke_even_odd";
        case pdf::PDFPageContentProcessor::Operator::PathClear:
            return "path_clear";
        case pdf::PDFPageContentProcessor::Operator::ClipWinding:
            return "clip_winding";
        case pdf::PDFPageContentProcessor::Operator::ClipEvenOdd:
            return "clip_even_odd";
        case pdf::PDFPageContentProcessor::Operator::TextBegin:
            return "text_begin";
        case pdf::PDFPageContentProcessor::Operator::TextEnd:
            return "text_end";
        case pdf::PDFPageContentProcessor::Operator::TextSetCharacterSpacing:
            return "set_char_spacing";
        case pdf::PDFPageContentProcessor::Operator::TextSetWordSpacing:
            return "set_word_spacing";
        case pdf::PDFPageContentProcessor::Operator::TextSetHorizontalScale:
            return "set_hor_scale";
        case pdf::PDFPageContentProcessor::Operator::TextSetLeading:
            return "set_leading";
        case pdf::PDFPageContentProcessor::Operator::TextSetFontAndFontSize:
            return "set_font";
        case pdf::PDFPageContentProcessor::Operator::TextSetRenderMode:
            return "set_text_render_mode";
        case pdf::PDFPageContentProcessor::Operator::TextSetRise:
            return "set_text_rise";
        case pdf::PDFPageContentProcessor::Operator::TextMoveByOffset:
            return "text_move_by_offset";
        case pdf::PDFPageContentProcessor::Operator::TextSetLeadingAndMoveByOffset:
            return "text_set_leading_and_move_by_offset";
        case pdf::PDFPageContentProcessor::Operator::TextSetMatrix:
            return "text_set_matrix";
        case pdf::PDFPageContentProcessor::Operator::TextMoveByLeading:
            return "text_move_by_leading";
        case pdf::PDFPageContentProcessor::Operator::TextShowTextString:
            return "text_show_string";
        case pdf::PDFPageContentProcessor::Operator::TextShowTextIndividualSpacing:
            return "text_show_string_with_spacing";
        case pdf::PDFPageContentProcessor::Operator::TextNextLineShowText:
            return "text_next_line_and_show_text";
        case pdf::PDFPageContentProcessor::Operator::TextSetSpacingAndShowText:
            return "text_set_spacing_and_show_text";
        case pdf::PDFPageContentProcessor::Operator::Type3FontSetOffset:
            return "text_t3_set_offset";
        case pdf::PDFPageContentProcessor::Operator::Type3FontSetOffsetAndBB:
            return "text_t3_set_offset_and_bb";
        case pdf::PDFPageContentProcessor::Operator::ColorSetStrokingColorSpace:
            return "set_stroke_color_space";
        case pdf::PDFPageContentProcessor::Operator::ColorSetFillingColorSpace:
            return "set_filling_color_space";
        case pdf::PDFPageContentProcessor::Operator::ColorSetStrokingColor:
            return "set_stroke_color";
        case pdf::PDFPageContentProcessor::Operator::ColorSetStrokingColorN:
            return "set_stroke_color_n";
        case pdf::PDFPageContentProcessor::Operator::ColorSetFillingColor:
            return "set_filling_color";
        case pdf::PDFPageContentProcessor::Operator::ColorSetFillingColorN:
            return "set_filling_color_n";
        case pdf::PDFPageContentProcessor::Operator::ColorSetDeviceGrayStroking:
            return "set_stroke_gray_cs";
        case pdf::PDFPageContentProcessor::Operator::ColorSetDeviceGrayFilling:
            return "set_filling_gray_cs";
        case pdf::PDFPageContentProcessor::Operator::ColorSetDeviceRGBStroking:
            return "set_stroke_rgb_cs";
        case pdf::PDFPageContentProcessor::Operator::ColorSetDeviceRGBFilling:
            return "set_filling_rgb_cs";
        case pdf::PDFPageContentProcessor::Operator::ColorSetDeviceCMYKStroking:
            return "set_stroke_cmyk_cs";
        case pdf::PDFPageContentProcessor::Operator::ColorSetDeviceCMYKFilling:
            return "set_filling_cmyk_cs";
        case pdf::PDFPageContentProcessor::Operator::ShadingPaintShape:
            return "shading_paint";
        case pdf::PDFPageContentProcessor::Operator::InlineImageBegin:
            return "ib";
        case pdf::PDFPageContentProcessor::Operator::InlineImageData:
            return "id";
        case pdf::PDFPageContentProcessor::Operator::InlineImageEnd:
            return "ie";
        case pdf::PDFPageContentProcessor::Operator::PaintXObject:
            return "paint_object";
        case pdf::PDFPageContentProcessor::Operator::MarkedContentPoint:
            return "mc_point";
        case pdf::PDFPageContentProcessor::Operator::MarkedContentPointWithProperties:
            return "mc_point_prop";
        case pdf::PDFPageContentProcessor::Operator::MarkedContentBegin:
            return "mc_begin";
        case pdf::PDFPageContentProcessor::Operator::MarkedContentBeginWithProperties:
            return "mc_begin_prop";
        case pdf::PDFPageContentProcessor::Operator::MarkedContentEnd:
            return "mc_end";
        case pdf::PDFPageContentProcessor::Operator::CompatibilityBegin:
            return "compat_begin";
        case pdf::PDFPageContentProcessor::Operator::CompatibilityEnd:
            return "compat_end";

        default:
            break;
    }

    return QString();
}

QString PDFEditedPageContent::getOperandName(PDFPageContentProcessor::Operator operatorValue, int operandIndex)
{
    static const std::map<std::pair<PDFPageContentProcessor::Operator, int>, QString> operands =
    {
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::SetLineWidth, 0), "lineWidth" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::SetLineCap, 0), "lineCap" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::SetLineJoin, 0), "lineJoin" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::SetMitterLimit, 0), "mitterLimit" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::SetRenderingIntent, 0), "renderingIntent" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::SetFlatness, 0), "flatness" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::SetGraphicState, 0), "graphicState" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::AdjustCurrentTransformationMatrix, 0), "a" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::AdjustCurrentTransformationMatrix, 1), "b" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::AdjustCurrentTransformationMatrix, 2), "c" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::AdjustCurrentTransformationMatrix, 3), "d" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::AdjustCurrentTransformationMatrix, 4), "e" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::AdjustCurrentTransformationMatrix, 5), "f" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::MoveCurrentPoint, 0), "x" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::MoveCurrentPoint, 1), "y" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::LineTo, 0), "x" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::LineTo, 1), "y" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::Bezier123To, 0), "x1" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::Bezier123To, 1), "y1" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::Bezier123To, 2), "x2" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::Bezier123To, 3), "y2" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::Bezier123To, 4), "x3" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::Bezier123To, 5), "y3" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::Bezier23To, 0), "x2" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::Bezier23To, 1), "y2" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::Bezier23To, 2), "x3" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::Bezier23To, 3), "y3" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::Bezier13To, 0), "x1" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::Bezier13To, 1), "y1" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::Bezier13To, 2), "x3" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::Bezier13To, 3), "y3" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::Rectangle, 0), "x" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::Rectangle, 1), "y" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::Rectangle, 2), "width" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::Rectangle, 3), "height" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextSetCharacterSpacing, 0), "charSpacing" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextSetWordSpacing, 0), "wordSpacing" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextSetHorizontalScale, 0), "scale" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextSetLeading, 0), "leading" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextSetFontAndFontSize, 0), "font" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextSetFontAndFontSize, 1), "fontSize" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextSetRenderMode, 0), "renderMode" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextSetRise, 0), "rise" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextMoveByOffset, 0), "tx" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextMoveByOffset, 1), "ty" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextSetLeadingAndMoveByOffset, 0), "tx" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextSetLeadingAndMoveByOffset, 1), "ty" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextSetMatrix, 0), "a" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextSetMatrix, 1), "b" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextSetMatrix, 2), "c" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextSetMatrix, 3), "d" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextSetMatrix, 4), "e" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextSetMatrix, 5), "f" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextShowTextString, 0), "string" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextNextLineShowText, 0), "string" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextShowTextIndividualSpacing, 0), "wSpacing" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextShowTextIndividualSpacing, 1), "chSpacing" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextShowTextIndividualSpacing, 2), "string" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextSetSpacingAndShowText, 0), "string" },
    };

    auto it = operands.find(std::make_pair(operatorValue, operandIndex));
    if (it != operands.cend())
    {
        return it->second;
    }

    return QString("op%1").arg(operandIndex);

    /*
        case pdf::PDFPageContentProcessor::Operator::Type3FontSetOffset:
            break;
        case pdf::PDFPageContentProcessor::Operator::Type3FontSetOffsetAndBB:
            break;
        case pdf::PDFPageContentProcessor::Operator::ColorSetStrokingColorSpace:
            break;
        case pdf::PDFPageContentProcessor::Operator::ColorSetFillingColorSpace:
            break;
        case pdf::PDFPageContentProcessor::Operator::ColorSetStrokingColor:
            break;
        case pdf::PDFPageContentProcessor::Operator::ColorSetStrokingColorN:
            break;
        case pdf::PDFPageContentProcessor::Operator::ColorSetFillingColor:
            break;
        case pdf::PDFPageContentProcessor::Operator::ColorSetFillingColorN:
            break;
        case pdf::PDFPageContentProcessor::Operator::ColorSetDeviceGrayStroking:
            break;
        case pdf::PDFPageContentProcessor::Operator::ColorSetDeviceGrayFilling:
            break;
        case pdf::PDFPageContentProcessor::Operator::ColorSetDeviceRGBStroking:
            break;
        case pdf::PDFPageContentProcessor::Operator::ColorSetDeviceRGBFilling:
            break;
        case pdf::PDFPageContentProcessor::Operator::ColorSetDeviceCMYKStroking:
            break;
        case pdf::PDFPageContentProcessor::Operator::ColorSetDeviceCMYKFilling:
            break;
        case pdf::PDFPageContentProcessor::Operator::ShadingPaintShape:
            break;
        case pdf::PDFPageContentProcessor::Operator::InlineImageBegin:
            break;
        case pdf::PDFPageContentProcessor::Operator::InlineImageData:
            break;
        case pdf::PDFPageContentProcessor::Operator::InlineImageEnd:
            break;
        case pdf::PDFPageContentProcessor::Operator::PaintXObject:
            break;
        case pdf::PDFPageContentProcessor::Operator::MarkedContentPoint:
            break;
        case pdf::PDFPageContentProcessor::Operator::MarkedContentPointWithProperties:
            break;
        case pdf::PDFPageContentProcessor::Operator::MarkedContentBegin:
            break;
        case pdf::PDFPageContentProcessor::Operator::MarkedContentBeginWithProperties:
            break;
        case pdf::PDFPageContentProcessor::Operator::MarkedContentEnd:
            break;
        case pdf::PDFPageContentProcessor::Operator::CompatibilityBegin:
            break;
        case pdf::PDFPageContentProcessor::Operator::CompatibilityEnd:
            break;*/
}

}   // namespace pdf
