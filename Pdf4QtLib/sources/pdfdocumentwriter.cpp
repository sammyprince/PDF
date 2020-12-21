//    Copyright (C) 2020 Jakub Melka
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
//    along with Pdf4Qt. If not, see <https://www.gnu.org/licenses/>.

#include "pdfdocumentwriter.h"
#include "pdfconstants.h"
#include "pdfvisitor.h"
#include "pdfparser.h"

#include <QFile>
#include <QSaveFile>

namespace pdf
{

class PDFWriteObjectVisitor : public PDFAbstractVisitor
{
public:
    explicit PDFWriteObjectVisitor(QIODevice* device) :
        m_device(device)
    {

    }

    virtual void visitNull() override;
    virtual void visitBool(bool value) override;
    virtual void visitInt(PDFInteger value) override;
    virtual void visitReal(PDFReal value) override;
    virtual void visitString(PDFStringRef string) override;
    virtual void visitName(PDFStringRef name) override;
    virtual void visitArray(const PDFArray* array) override;
    virtual void visitDictionary(const PDFDictionary* dictionary) override;
    virtual void visitStream(const PDFStream* stream) override;
    virtual void visitReference(const PDFObjectReference reference) override;

    PDFObject getDecryptedObject();

private:
    void writeName(const QByteArray& string);

    QIODevice* m_device;
};

void PDFWriteObjectVisitor::visitNull()
{
    m_device->write("null ");
}

void PDFWriteObjectVisitor::visitBool(bool value)
{
    if (value)
    {
        m_device->write("true ");
    }
    else
    {
        m_device->write("false ");
    }
}

void PDFWriteObjectVisitor::visitInt(PDFInteger value)
{
    m_device->write(QString::number(value).toLatin1());
    m_device->write(" ");
}

void PDFWriteObjectVisitor::visitReal(PDFReal value)
{
    // Jakub Melka: we use 5 digits, because they are specified
    // in PDF 1.7 specification, appendix C, Table C.1, where it is defined,
    // that number of significant digits of precision is 5.
    m_device->write(QString::number(value, 'f', 5).toLatin1());
    m_device->write(" ");
}

void PDFWriteObjectVisitor::visitString(PDFStringRef string)
{
    QByteArray data = string.getString();
    if (data.indexOf('(') != -1 ||
        data.indexOf(')') != -1 ||
        data.indexOf('\\') != -1)
    {
        m_device->write("<");
        m_device->write(data.toHex());
        m_device->write(">");
    }
    else
    {
        m_device->write("(");
        m_device->write(data);
        m_device->write(")");
    }

    m_device->write(" ");
}

void PDFWriteObjectVisitor::writeName(const QByteArray& string)
{
    m_device->write("/");

    for (const char character : string)
    {
        if (PDFLexicalAnalyzer::isRegular(character))
        {
            m_device->write(&character, 1);
        }
        else
        {
            m_device->write("#");
            m_device->write(QByteArray(&character, 1).toHex());
        }
    }

    m_device->write(" ");
}

void PDFWriteObjectVisitor::visitName(PDFStringRef name)
{
    writeName(name.getString());
}

void PDFWriteObjectVisitor::visitArray(const PDFArray* array)
{
    m_device->write("[ ");
    acceptArray(array);
    m_device->write("] ");
}

void PDFWriteObjectVisitor::visitDictionary(const PDFDictionary* dictionary)
{
    m_device->write("<< ");

    for (size_t i = 0, count = dictionary->getCount(); i < count; ++i)
    {
        writeName(dictionary->getKey(i).getString());
        dictionary->getValue(i).accept(this);
    }

    m_device->write(">> ");
}

void PDFWriteObjectVisitor::visitStream(const PDFStream* stream)
{
    visitDictionary(stream->getDictionary());

    m_device->write("stream");
    m_device->write("\x0D\x0A");
    m_device->write(*stream->getContent());
    m_device->write("\x0D\x0A");
    m_device->write("endstream");
    m_device->write("\x0D\x0A");
}

void PDFWriteObjectVisitor::visitReference(const PDFObjectReference reference)
{
    visitInt(reference.objectNumber);
    visitInt(reference.generation);
    m_device->write("R ");
}

PDFOperationResult PDFDocumentWriter::write(const QString& fileName, const PDFDocument* document, bool safeWrite)
{
    if (safeWrite)
    {
        QSaveFile file(fileName);
        file.setDirectWriteFallback(true);

        if (file.open(QFile::WriteOnly | QFile::Truncate))
        {
            PDFOperationResult result = write(&file, document);
            if (result)
            {
                file.commit();
            }
            else
            {
                file.cancelWriting();
            }
            return result;
        }
        else
        {
            return tr("File '%1' can't be opened for writing. %2").arg(fileName, file.errorString());
        }
    }
    else
    {
        QFile file(fileName);

        if (file.open(QFile::WriteOnly | QFile::Truncate))
        {
            PDFOperationResult result = write(&file, document);
            file.close();
            return result;
        }
        else
        {
            return tr("File '%1' can't be opened for writing. %2").arg(fileName, file.errorString());
        }
    }
}

PDFOperationResult PDFDocumentWriter::write(QIODevice* device, const PDFDocument* document)
{
    if (!device->isWritable())
    {
        return tr("Device is not writable.");
    }

    const PDFObjectStorage& storage = document->getStorage();
    const PDFObjectStorage::PDFObjects& objects = storage.getObjects();
    const size_t objectCount = objects.size();
    if (storage.getSecurityHandler()->getMode() != EncryptionMode::None)
    {
        return tr("Writing of encrypted documents is not supported.");
    }

    // Write header
    PDFVersion version = document->getInfo()->version;
    device->write(QString("%PDF-%1.%2").arg(version.major).arg(version.minor).toLatin1());
    writeCRLF(device);
    device->write("% PDF producer: ");
    device->write(PDF_LIBRARY_NAME);
    writeCRLF(device);
    writeCRLF(device);
    writeCRLF(device);

    // Write objects
    std::vector<PDFInteger> offsets(objectCount, -1);
    for (size_t i = 0; i < objectCount; ++i)
    {
        const PDFObjectStorage::Entry& entry = objects[i];
        if (entry.object.isNull())
        {
            continue;
        }

        // Jakub Melka: we must mark actual position of object
        offsets[i] = device->pos();

        PDFWriteObjectVisitor visitor(device);
        writeObjectHeader(device, PDFObjectReference(i, entry.generation));
        entry.object.accept(&visitor);
        writeObjectFooter(device);
    }

    // Write cross-reference table
    PDFInteger xrefOffset = device->pos();
    device->write("xref");
    writeCRLF(device);
    device->write(QString("0 %1").arg(objectCount).toLatin1());
    writeCRLF(device);

    for (size_t i = 0; i < objectCount; ++i)
    {
        const PDFObjectStorage::Entry& entry = objects[i];
        PDFInteger generation = entry.generation;

        if (i == 0)
        {
            generation = 65535;
        }

        PDFInteger offset = offsets[i];
        if (offset == -1)
        {
            offset = 0;
        }

        QString offsetString = QString::number(offset).rightJustified(10, QChar('0'), true);
        QString generationString = QString::number(generation).rightJustified(5, QChar('0'), true);

        device->write(offsetString.toLatin1());
        device->write(" ");
        device->write(generationString.toLatin1());
        device->write(" ");
        device->write(entry.object.isNull() ? "f" : "n");
        writeCRLF(device);
    }
    device->write("trailer");
    writeCRLF(device);
    PDFWriteObjectVisitor trailerVisitor(device);
    storage.getTrailerDictionary().accept(&trailerVisitor);
    writeCRLF(device);
    device->write("startxref");
    writeCRLF(device);
    device->write(QString::number(xrefOffset).toLatin1());
    writeCRLF(device);

    // Write footer
    device->write("%%EOF");

    return true;
}

void PDFDocumentWriter::writeCRLF(QIODevice* device)
{
    device->write("\x0D\x0A");
}

void PDFDocumentWriter::writeObjectHeader(QIODevice* device, PDFObjectReference reference)
{
    QString objectHeader = QString("%1 %2 obj").arg(QString::number(reference.objectNumber)).arg(QString::number(reference.generation));
    device->write(objectHeader.toLatin1());
    writeCRLF(device);
}

void PDFDocumentWriter::writeObjectFooter(QIODevice* device)
{
    device->write("endobj");
    writeCRLF(device);
}

class PDFSizeCounterIODevice : public QIODevice
{
public:
    explicit PDFSizeCounterIODevice(QObject* parent) :
        QIODevice(parent)
    {

    }

    virtual bool isSequential() const override;
    virtual bool open(OpenMode mode) override;
    virtual void close() override;
    virtual qint64 pos() const override;
    virtual qint64 size() const override;
    virtual bool seek(qint64 pos) override;
    virtual bool atEnd() const override;
    virtual bool reset() override;
    virtual qint64 bytesAvailable() const override;
    virtual qint64 bytesToWrite() const override;
    virtual bool canReadLine() const override;
    virtual bool waitForReadyRead(int msecs) override;
    virtual bool waitForBytesWritten(int msecs) override;

protected:
    virtual qint64 readData(char* data, qint64 maxlen) override;
    virtual qint64 readLineData(char* data, qint64 maxlen) override;
    virtual qint64 writeData(const char* data, qint64 len) override;

private:
    OpenMode m_openMode = NotOpen;
    qint64 m_fileSize = 0;
};

bool PDFSizeCounterIODevice::isSequential() const
{
    return true;
}

bool PDFSizeCounterIODevice::open(OpenMode mode)
{
    if (m_openMode == NotOpen)
    {
        setOpenMode(mode);
        return true;
    }
    else
    {
        return false;
    }
}

void PDFSizeCounterIODevice::close()
{
    setOpenMode(NotOpen);
}

qint64 PDFSizeCounterIODevice::pos() const
{
    return m_fileSize;
}

qint64 PDFSizeCounterIODevice::size() const
{
    return m_fileSize;
}

bool PDFSizeCounterIODevice::seek(qint64 pos)
{
    Q_UNUSED(pos);

    return false;
}

bool PDFSizeCounterIODevice::atEnd() const
{
    return true;
}

bool PDFSizeCounterIODevice::reset()
{
    return false;
}

qint64 PDFSizeCounterIODevice::bytesAvailable() const
{
    return 0;
}

qint64 PDFSizeCounterIODevice::bytesToWrite() const
{
    return 0;
}

bool PDFSizeCounterIODevice::canReadLine() const
{
    return false;
}

bool PDFSizeCounterIODevice::waitForReadyRead(int msecs)
{
    Q_UNUSED(msecs);

    return false;
}

bool PDFSizeCounterIODevice::waitForBytesWritten(int msecs)
{
    Q_UNUSED(msecs);

    return false;
}

qint64 PDFSizeCounterIODevice::readData(char* data, qint64 maxlen)
{
    Q_UNUSED(data);
    Q_UNUSED(maxlen);

    return 0;
}

qint64 PDFSizeCounterIODevice::readLineData(char* data, qint64 maxlen)
{
    Q_UNUSED(data);
    Q_UNUSED(maxlen);

    return 0;
}

qint64 PDFSizeCounterIODevice::writeData(const char* data, qint64 len)
{
    Q_UNUSED(data);

    m_fileSize += len;
    return len;
}

qint64 PDFDocumentWriter::getDocumentFileSize(const PDFDocument* document)
{
    PDFSizeCounterIODevice device(nullptr);
    PDFDocumentWriter writer(nullptr);

    device.open(QIODevice::WriteOnly);

    if (writer.write(&device, document))
    {
        device.close();
        return device.pos();
    }

    device.close();
    return -1;
}

}   // namespace pdf