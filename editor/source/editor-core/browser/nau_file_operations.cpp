// Copyright 2024 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.

#include "nau_file_operations.hpp"
#include "nau_project_browser_file_system_model.hpp"
#include "nau_widget_utility.hpp"
#include "nau_log.hpp"

#include "nau/assets/nau_asset_manager.hpp"
#include "nau/nau_editor_plugin_manager.hpp"

#include <QApplication>
#include <QRegularExpression>
#include <QClipboard>
#include <QMimeData>
#include <QUrl>

#include <filesystem>
#include <system_error>


// ** NauFileOperations

void NauFileOperations::copyToClipboard(const QModelIndexList& indexes)
{
    auto mimeData = prepareCopyCut(indexes);
    if (!mimeData) return;

    mimeData->setData(cutOperationMimeType(), copyDropEffectData());
    QApplication::clipboard()->setMimeData(mimeData);
}

void NauFileOperations::cutToClipboard(const QModelIndexList& indexes)
{
    auto mimeData = prepareCopyCut(indexes);
    if (!mimeData) return;

    mimeData->setData(cutOperationMimeType(), cutDropEffectData());
    QApplication::clipboard()->setMimeData(mimeData);
}

void NauFileOperations::pasteFromClipboard(const QModelIndex& parent)
{
    auto clipboard = QApplication::clipboard();
    auto mimeData = clipboard->mimeData();
    if (!mimeData || !mimeData->hasUrls()) return;

    const bool cutting = mimeData->data(cutOperationMimeType()) == cutDropEffectData();
    const QString path = parent.data(NauProjectBrowserFileSystemModel::FilePathRole).toString();
    const QFileInfo fi{path};
    const NauDir dstPath =  fi.isDir() ? path : fi.absolutePath();

    for (const auto& url : mimeData->urls()) {
        const QString srcPath = url.toLocalFile();
        const bool isDir = QFileInfo{srcPath}.isDir();

        if (isDir && dstPath.absolutePath().startsWith(srcPath)) {
            NED_WARNING("Unable to {} {} to {}. Source and Destination is the same.",
                (cutting ? "move" : "copy"), srcPath.toUtf8().constData(), dstPath.absolutePath().toUtf8().constData());
            continue;
        }

        const QString srcFileName = QFileInfo{srcPath}.fileName();
        const QString dstFileName = !cutting 
            ? generateFileNameIfExists(dstPath.absoluteFilePath(srcFileName))
            : dstPath.absoluteFilePath(srcFileName);

        bool result = false;
        if (cutting) {
            result = isDir ? movePathRecursively(srcPath, dstFileName) : moveAssetPair(srcPath, dstFileName);
        } else if (!isDir && duplicateAssetPair(srcPath, dstPath.absolutePath())) {
            // A paired meta is duplicated together with its source file as an independent asset.
            result = true;
        } else {
            result = isDir ? copyPathRecursively(srcPath, dstFileName) : QFile::copy(srcPath, dstFileName);
        }

        if (!result) {
            NED_ERROR("Failed to {} {} to {}", (cutting ? "move" : "copy"), srcPath.toUtf8().constData(),
                dstFileName.toUtf8().constData());
        } else {
            NED_TRACE("Successfuly {} {} to {}", (cutting ? "moving" : "coping"), srcPath.toUtf8().constData(),
                dstFileName.toUtf8().constData());
        }
    }

    clipboard->clear();
}

void NauFileOperations::duplicate(const QModelIndexList& indexes)
{
    for (const auto& index : indexes) {
        const QString srcPath = index.data(NauProjectBrowserFileSystemModel::FilePathRole).toString();
        const NauDir dstPath = QFileInfo{srcPath}.absoluteDir();
        const QString dstFileName = generateFileNameIfExists(srcPath);

        if (duplicateAssetPair(srcPath, dstPath.absolutePath())) {
            continue;
        }

        const bool result = QFileInfo{srcPath}.isDir()
            ? copyPathRecursively(srcPath, dstFileName)
            : QFile::copy(srcPath, dstFileName);

        if (!result) {
            NED_ERROR("Failed to duplicate {} to {}", srcPath.toUtf8().constData(), dstFileName.toUtf8().constData());
        } else {
            NED_TRACE("Duplicated {} to {}", srcPath.toUtf8().constData(), dstFileName.toUtf8().constData());
        }
    }
}

QString NauFileOperations::generateFileNameIfExists(const QString& absFilePath)
{
    auto result = absFilePath;

    const QFileInfo info{result};
    const NauDir dir = info.absoluteDir();

    QString newFileName = info.fileName();

    while (QFile::exists(result)) {
        newFileName = generateFileName(newFileName);
        result = dir.absoluteFilePath(newFileName);
    }

    return result;
}

bool NauFileOperations::duplicateAssetPair(const QString& srcMetaPath, const QString& dstDir)
{
    static const QString metaExtension = QStringLiteral(".nausd");
    if (!srcMetaPath.endsWith(metaExtension)) {
        return false;
    }

    const QString srcSourcePath = QString(srcMetaPath).chopped(metaExtension.size());
    const QFileInfo srcSourceInfo{srcSourcePath};
    if (!srcSourceInfo.exists() || srcSourceInfo.isDir()) {
        // Not a paired meta (e.g. a self-contained material).
        return false;
    }

    const QString newSourcePath = generateFileNameIfExists(NauDir{dstDir}.absoluteFilePath(srcSourceInfo.fileName()));
    const QString newMetaPath = newSourcePath + metaExtension;
    if (QFile::exists(newMetaPath)) {
        return false;
    }

    if (!Nau::EditorServiceProvider().get<NauAssetFileProcessorInterface>().duplicatePairedAsset(
            srcMetaPath.toStdString(), newMetaPath.toStdString(), newSourcePath.toStdString()))
    {
        return false;
    }

    NED_TRACE("Duplicated asset {} to {}", srcMetaPath.toUtf8().constData(), newMetaPath.toUtf8().constData());
    return true;
}

bool NauFileOperations::moveAssetPair(const QString& srcMetaPath, const QString& dstMetaPath)
{
    static const QString metaExtension = QStringLiteral(".nausd");
    if (!srcMetaPath.endsWith(metaExtension)) {
        return QFile::rename(srcMetaPath, dstMetaPath);
    }

    const QString srcSourcePath = QString(srcMetaPath).chopped(metaExtension.size());
    if (!QFile::exists(srcSourcePath)) {
        return QFile::rename(srcMetaPath, dstMetaPath);
    }

    const QString dstSourcePath = QString(dstMetaPath).chopped(metaExtension.size());
    if (QFile::exists(dstSourcePath)) {
        NED_WARNING("Asset source file {} already exists, moving the meta without its source",
            dstSourcePath.toUtf8().constData());
        return QFile::rename(srcMetaPath, dstMetaPath);
    }

    if (!QFile::rename(srcSourcePath, dstSourcePath)) {
        return QFile::rename(srcMetaPath, dstMetaPath);
    }
    if (!QFile::rename(srcMetaPath, dstMetaPath)) {
        QFile::rename(dstSourcePath, srcSourcePath);
        return false;
    }

    // QFile::rename keeps the source file modification time, so the import pipeline would treat
    // the moved asset as unchanged and skip updating its database record (with the new relative
    // path). Bump the time stamp so the watcher-driven reimport updates the record.
    std::error_code ec;
    std::filesystem::last_write_time(dstSourcePath.toStdString(), std::filesystem::file_time_type::clock::now(), ec);

    return true;
}

QString NauFileOperations::cutOperationMimeType()
{
#ifndef Q_OS_WIN
#warning Cut operation (i.e. drop effect) from/to OS File Browser is implemented for Win.
#endif // !Q_OS_WIN

    return QStringLiteral("Preferred DropEffect");
}

QByteArray NauFileOperations::copyDropEffectData()
{
    return QByteArray::fromRawData("\x05\x00\x00\x00", 4);
}

QByteArray NauFileOperations::cutDropEffectData()
{
    return QByteArray::fromRawData("\x02\x00\x00\x00", 4);
}

QMimeData* NauFileOperations::prepareCopyCut(const QModelIndexList& indexes)
{
    if (indexes.isEmpty()) return nullptr;

    QList<QUrl> urls;

    for (const auto& index : indexes) {
        const QString path = index.data(NauProjectBrowserFileSystemModel::FilePathRole).toString();
        urls.append(QUrl::fromLocalFile(path));

        NED_TRACE("Prepare \"{}\" copy/cut through clipboard", path.toUtf8().constData());
    }

    auto mimeData = new QMimeData;
    mimeData->setUrls(urls);

    return mimeData;
}

bool NauFileOperations::copyPathRecursively(const QString& src, const QString& dst, bool overwrite)
{
    const NauDir srcDir(src);
    if (!srcDir.exists()) return false;

    if (!NauDir().mkpath(dst)) return false;
    const NauDir dstDir = dst;

    for (const QString& dirEntry : srcDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!copyPathRecursively(srcDir.absoluteFilePath(dirEntry), dstDir.absoluteFilePath(dirEntry), overwrite)) return false;
    }

    for (const QString& fileEntry : srcDir.entryList(QDir::Files)) {
        const QString dstAbsFileName = dstDir.absoluteFilePath(fileEntry);

        if (overwrite && NauFile::exists(dstAbsFileName)) {
            NauFile::remove(dstAbsFileName);
        }

        if (!QFile::copy(srcDir.absoluteFilePath(fileEntry), dstAbsFileName)) { 
            return false;
        }
    }

    return true;
}

QString NauFileOperations::generateFileName(const QString& fileName)
{
    const QFileInfo info {fileName} ;
    static const auto dotSeparator = QStringLiteral(".");

    QString extension = info.completeSuffix().isEmpty() ? QString() : dotSeparator + info.completeSuffix();
    if (fileName.endsWith(dotSeparator)) {
        extension = dotSeparator;
    }

    const NauDir dir = info.absoluteDir();
    QString baseFileName = info.baseName();

    static const QRegularExpression rx{"^(.*?)\\((\\d*?)\\)$"};
    int counter = 1;

    const auto match = rx.match(baseFileName);

    // if a name has format 'abcd(number)', we increment number, that is contained in it.
    // 1st matched group is a base file name, 2nd is a counter.
    // e.g.: 'test(5)' -> {1:'test', 2:5}
    if (match.hasMatch() && match.hasCaptured(1) && match.hasCaptured(2)) {
        baseFileName = match.captured(1);
        counter = match.captured(2).toInt() + 1;
    }

    static const auto templateStr = QStringLiteral("%1(%2)%3");
    return templateStr.arg(baseFileName).arg(counter).arg(extension);
}

bool NauFileOperations::movePathRecursively(const QString& src, const QString& dst)
{
    NauDir srcDir(src);
    if (!srcDir.exists()) return false;

    NauDir dstDir = dst;
    dstDir.mkpath(dst);

    for (const QString& dirEntry : srcDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!movePathRecursively(srcDir.absoluteFilePath(dirEntry), dstDir.absoluteFilePath(dirEntry))) return false;
    }

    for (const QString& fileEntry : srcDir.entryList(QDir::Files)) {
        if (!QFile::rename(srcDir.absoluteFilePath(fileEntry), dstDir.absoluteFilePath(fileEntry))) return false;
    }

    return NauDir().rmdir(src);
}

bool NauFileOperations::deletePathRecursively(const QString& path)
{
    const NauDir dir(path);
    for (const QString& dirEntry : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!deletePathRecursively(dir.absoluteFilePath(dirEntry))) return false;
    }

    return QFile::moveToTrash(path);
}

