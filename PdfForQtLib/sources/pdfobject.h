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


#ifndef PDFOBJECT_H
#define PDFOBJECT_H

#include "pdfglobal.h"

#include <QByteArray>

#include <memory>
#include <vector>
#include <variant>

namespace pdf
{

/// This class represents a content of the PDF object. It can be
/// array of objects, dictionary, content stream data, or string data.
class PDFObjectContent
{
public:
    constexpr PDFObjectContent() = default;
    virtual ~PDFObjectContent() = default;

    /// Equals operator. Returns true, if content of this object is
    /// equal to the content of the other object.
    virtual bool equals(const PDFObjectContent* other) const = 0;
};

class PDFFORQTLIBSHARED_EXPORT PDFObject
{
public:
    enum class Type
    {
        // Simple PDF objects
        Null,
        Bool,
        Int,
        Real,
        String,
        Name,

        // Complex PDF objects
        Array,
        Dictionary,
        Stream,
        Reference
    };

    typedef std::shared_ptr<PDFObjectContent> PDFObjectContentPointer;

    // Default constructor should be constexpr
    constexpr inline PDFObject() :
        m_type(Type::Null),
        m_data()
    {

    }

    // Default destructor should be OK
    inline ~PDFObject() = default;

    // Enforce default copy constructor and default move constructor
    constexpr inline PDFObject(const PDFObject&) = default;
    constexpr inline PDFObject(PDFObject&&) = default;

    // Enforce default copy assignment operator and move assignment operator
    constexpr inline PDFObject& operator=(const PDFObject&) = default;
    constexpr inline PDFObject& operator=(PDFObject&&) = default;

    // Test operators
    inline bool isNull() const { return m_type == Type::Null; }
    inline bool isBool() const { return m_type == Type::Bool; }
    inline bool isInt() const { return m_type == Type::Int; }
    inline bool isReal() const { return m_type == Type::Real; }
    inline bool isString() const { return m_type == Type::String; }
    inline bool isName() const { return m_type == Type::Name; }
    inline bool isArray() const { return m_type == Type::Array; }
    inline bool isDictionary() const { return m_type == Type::Dictionary; }
    inline bool isStream() const { return m_type == Type::Stream; }
    inline bool isReference() const { return m_type == Type::Reference; }

    inline PDFInteger getInteger() const { return std::get<PDFInteger>(m_data); }
    QByteArray getString() const;

    bool operator==(const PDFObject& other) const;
    bool operator!=(const PDFObject& other) const { return !(*this == other); }

    /// Creates a null object
    static inline PDFObject createNull() { return PDFObject(); }

    /// Creates a boolean object
    static inline PDFObject createBool(bool value) { return PDFObject(Type::Bool, value); }

    /// Creates an integer object
    static inline PDFObject createInteger(PDFInteger value) { return PDFObject(Type::Int, value); }

    /// Creates an object with real number
    static inline PDFObject createReal(PDFReal value) { return PDFObject(Type::Real, value); }

    /// Creates a name object
    static inline PDFObject createName(PDFObjectContentPointer&& value) { return PDFObject(Type::Name, std::move(value)); }

    /// Creates a reference object
    static inline PDFObject createReference(const PDFObjectReference& reference) { return PDFObject(Type::Reference, reference); }

    /// Creates a string object
    static inline PDFObject createString(PDFObjectContentPointer&& value) { return PDFObject(Type::String, std::move(value)); }

    /// Creates an array object
    static inline PDFObject createArray(PDFObjectContentPointer&& value) { return PDFObject(Type::Array, std::move(value)); }

    /// Creates a dictionary object
    static inline PDFObject createDictionary(PDFObjectContentPointer&& value) { return PDFObject(Type::Dictionary, std::move(value)); }

    /// Creates a stream object
    static inline PDFObject createStream(PDFObjectContentPointer&& value) { return PDFObject(Type::Stream, std::move(value)); }

private:
    template<typename T>
    constexpr inline PDFObject(Type type, T&& value) :
        m_type(type),
        m_data(std::forward<T>(value))
    {

    }

    Type m_type;
    std::variant<typename std::monostate, bool, PDFInteger, PDFReal, PDFObjectReference, PDFObjectContentPointer> m_data;
};

/// Represents raw string in the PDF file. No conversions are performed, this is
/// reason, that we do not use QString, but QByteArray instead. QString is
/// encoded int UTF-8.
class PDFString : public PDFObjectContent
{
public:
    inline explicit PDFString() = default;
    inline explicit PDFString(QByteArray&& value) :
        m_string(std::move(value))
    {

    }

    virtual ~PDFString() override = default;

    virtual bool equals(const PDFObjectContent* other) const override;

    QByteArray getString() const;
    void setString(const QByteArray &getString);

private:
    QByteArray m_string;
};

/// Represents an array of objects in the PDF file.
class PDFArray : public PDFObjectContent
{
public:
    inline constexpr PDFArray() = default;
    virtual ~PDFArray() override = default;

    virtual bool equals(const PDFObjectContent* other) const override;

    /// Returns item at the specified index. If index is invalid,
    /// then it throws an exception.
    const PDFObject& getItem(size_t index) const { return m_objects.at(index); }

    /// Returns size of the array (number of elements)
    size_t getCount() const { return m_objects.size(); }

    /// Appends object to the end of object list
    void appendItem(PDFObject object);

private:
    std::vector<PDFObject> m_objects;
};

/// Represents a dictionary of objects in the PDF file. Dictionary is
/// an array of pairs key-value, where key is name object and value is any
/// PDF object. For this reason, we use QByteArray for key. We do not use
/// map, because dictionaries are usually small.
class PDFDictionary : public PDFObjectContent
{
private:
    using DictionaryEntry = std::pair<QByteArray, PDFObject>;

public:
    inline constexpr PDFDictionary() = default;
    virtual ~PDFDictionary() override = default;

    virtual bool equals(const PDFObjectContent* other) const override;

    /// Returns object for the key. If key is not found in the dictionary,
    /// then valid reference to the null object is returned.
    /// \param key Key
    const PDFObject& get(const QByteArray& key) const;

    /// Returns object for the key. If key is not found in the dictionary,
    /// then valid reference to the null object is returned.
    /// \param key Key
    const PDFObject& get(const char* key) const;

    /// Returns true, if dictionary contains a particular key
    /// \param key Key to be found in the dictionary
    bool hasKey(const QByteArray& key) const { return find(key) != m_dictionary.cend(); }

    /// Returns true, if dictionary contains a particular key
    /// \param key Key to be found in the dictionary
    bool hasKey(const char* key) const { return find(key) != m_dictionary.cend(); }

    /// Adds a new entry to the dictionary.
    /// \param key Key
    /// \param value Value
    void addEntry(QByteArray&& key, PDFObject&& value) { m_dictionary.emplace_back(std::move(key), std::move(value)); }

private:
    /// Finds an item in the dictionary array, if the item is not in the dictionary,
    /// then end iterator is returned.
    /// \param key Key to be found
    std::vector<DictionaryEntry>::const_iterator find(const QByteArray& key) const;

    /// Finds an item in the dictionary array, if the item is not in the dictionary,
    /// then end iterator is returned.
    /// \param key Key to be found
    std::vector<DictionaryEntry>::const_iterator find(const char* key) const;

    std::vector<DictionaryEntry> m_dictionary;
};

/// Represents a stream object in the PDF file. Stream consists of dictionary
/// and stream content - byte array.
class PDFStream : public PDFObjectContent
{
public:
    inline explicit constexpr PDFStream() = default;
    inline explicit PDFStream(PDFDictionary&& dictionary, QByteArray&& content) :
        m_dictionary(std::move(dictionary)),
        m_content(std::move(content))
    {

    }

    virtual ~PDFStream() override = default;

    virtual bool equals(const PDFObjectContent* other) const override;

    /// Returns dictionary for this content stream
    const PDFDictionary* getDictionary() const { return &m_dictionary; }

private:
    PDFDictionary m_dictionary;
    QByteArray m_content;
};

}   // namespace pdf

#endif // PDFOBJECT_H
