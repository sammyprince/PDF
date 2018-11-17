//    Copyright (C) 2018 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.


#ifndef PDFGLOBAL_H
#define PDFGLOBAL_H

#include <QtGlobal>

#include <limits>
#include <tuple>

#if defined(PDFFORQTLIB_LIBRARY)
#  define PDFFORQTLIBSHARED_EXPORT Q_DECL_EXPORT
#else
#  define PDFFORQTLIBSHARED_EXPORT Q_DECL_IMPORT
#endif

namespace pdf
{

using PDFInteger = int64_t;
using PDFReal = double;

// These constants define minimum/maximum integer and are defined in such a way,
// that even 100 times bigger integers are representable.

constexpr PDFInteger PDF_INTEGER_MIN = std::numeric_limits<int64_t>::min() / 100;
constexpr PDFInteger PDF_INTEGER_MAX = std::numeric_limits<int64_t>::max() / 100;

static constexpr bool isValidInteger(PDFInteger integer)
{
    return integer >= PDF_INTEGER_MIN && integer <= PDF_INTEGER_MAX;
}

/// This structure represents a reference to the object - consisting of the
/// object number, and generation number.
struct PDFObjectReference
{
    constexpr inline PDFObjectReference() :
        objectNumber(0),
        generation(0)
    {

    }

    constexpr inline PDFObjectReference(PDFInteger objectNumber, PDFInteger generation) :
        objectNumber(objectNumber),
        generation(generation)
    {

    }

    PDFInteger objectNumber;
    PDFInteger generation;

    constexpr bool operator==(const PDFObjectReference& other) const
    {
        return objectNumber == other.objectNumber && generation == other.generation;
    }

    constexpr bool operator!=(const PDFObjectReference& other) const { return !(*this == other); }

    constexpr bool operator<(const PDFObjectReference& other) const
    {
        return std::tie(objectNumber, generation) < std::tie(other.objectNumber, other.generation);
    }
};

/// Represents version identification
struct PDFVersion
{
    constexpr explicit PDFVersion() = default;
    constexpr explicit PDFVersion(uint16_t major, uint16_t minor) :
        major(major),
        minor(minor)
    {

    }

    uint16_t major = 0;
    uint16_t minor = 0;

    bool isValid() const { return major > 0; }
};

}   // namespace pdf

#endif // PDFGLOBAL_H
