//    Copyright (C) 2018-2019 Jakub Melka
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


#ifndef PDFNUMBERTREELOADER_H
#define PDFNUMBERTREELOADER_H

#include "pdfdocument.h"

#include <vector>

namespace pdf
{

/// This class can load a number tree into the array
template<typename Type>
class PDFNumberTreeLoader
{
public:
    explicit PDFNumberTreeLoader() = delete;

    using Objects = std::vector<Type>;

    /// Parses the number tree and loads its items into the array. Some errors are ignored,
    /// e.g. when kid is null. Type must contain methods to load object array.
    static Objects parse(const PDFDocument* document, const PDFObject& root)
    {
        Objects result;

        // First, try to load items from the tree into the array
        parseImpl(result, document, root);

        // Array may not be sorted. Sort it using comparison operator for Type.
        std::stable_sort(result.begin(), result.end());

        return result;
    }

private:
    static void parseImpl(Objects& objects, const PDFDocument* document, const PDFObject& root)
    {
        const PDFObject& dereferencedRoot = document->getObject(root);
        if (dereferencedRoot.isDictionary())
        {
            const PDFDictionary* dictionary = dereferencedRoot.getDictionary();

            // First, load the objects into the array
            const PDFObject& numberedItems = document->getObject(dictionary->get("Nums"));
            if (numberedItems.isArray())
            {
                const PDFArray* numberedItemsArray = numberedItems.getArray();
                const size_t count = numberedItemsArray->getCount() / 2;
                objects.reserve(objects.size() + count);
                for (size_t i = 0; i < count; ++i)
                {
                    const size_t numberIndex = 2 * i;
                    const size_t valueIndex = 2 * i + 1;

                    const PDFObject& number = document->getObject(numberedItemsArray->getItem(numberIndex));
                    if (!number.isInt())
                    {
                        continue;
                    }

                    objects.emplace_back(Type::parse(number.getInteger(), document, numberedItemsArray->getItem(valueIndex)));
                }
            }

            // Then, follow the kids
            const PDFObject&  kids = document->getObject(dictionary->get("Kids"));
            if (kids.isArray())
            {
                const PDFArray* kidsArray = kids.getArray();
                const size_t count = kidsArray->getCount();
                for (size_t i = 0; i < count; ++i)
                {
                    parseImpl(objects, document, kidsArray->getItem(i));
                }
            }
        }
    }
};

}   // namespace pdf

#endif // PDFNUMBERTREELOADER_H
