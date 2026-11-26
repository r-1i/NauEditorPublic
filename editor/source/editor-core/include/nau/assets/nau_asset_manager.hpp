// Copyright 2024 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
//
// Asset manager interface

#pragma once

#include "nau/nau_editor_config.hpp"
#include "nau/assets/nau_asset_manager_client.hpp"

#include "nau/assets/nau_file_types.hpp"
#include "browser/nau_project_browser_item_type.hpp"
#include "project/nau_project.hpp"

#include "nau/rtti/rtti_object.h"

#include <string>
#include <filesystem>


// ** NauAssetFileProcessorInterface
// Interface for asset file processor. Need to be implement for specific asset meta files

class NAU_EDITOR_API NauAssetFileProcessorInterface : virtual public nau::IRefCounted
{
    NAU_INTERFACE(NauAssetFileProcessorInterface, nau::IRefCounted)

public:
    virtual void setAssetFileUid(const std::filesystem::path& assetSource) = 0;
    virtual int importAsset(const std::filesystem::path& project, const std::filesystem::path& assetSource) = 0;

    // Replaces uids duplicated with other asset files and prepares renamed or moved assets for reimport.
    // Returns false if the meta file could not be processed, in which case the import must be aborted.
    virtual bool ensureUniqueUid(const std::filesystem::path& assetPath) = 0;

    // Renames a source asset file together with its meta file ("<name>.nausd"), rebinding the source
    // references inside the meta. Both paths must be in the same directory; the paired source of
    // oldAssetMetaPath must exist and the new paths must not. This keeps the asset uid and a single
    // asset database record, so the rename is not mistaken for a new asset on the next import.
    // Returns false with the files left unchanged on failure.
    virtual bool renamePairedAsset(const std::filesystem::path& oldAssetMetaPath, const std::filesystem::path& newAssetMetaPath) = 0;

    // Creates an independent copy of a source asset: copies the paired source file and exports the
    // meta with its source references rebound to the new source file. The caller provides unique new
    // paths; the meta copy keeps the original uid, which ensureUniqueUid replaces on import.
    // Returns false on failure (partially created files are removed).
    virtual bool duplicatePairedAsset(const std::filesystem::path& assetMetaPath, const std::filesystem::path& newAssetMetaPath, const std::filesystem::path& newSourcePath) = 0;

    // Reads asset meta file and gets source path info from it
    virtual std::string sourcePathFromAssetFile(const std::string& assetPath) = 0;

    virtual std::string_view assetFileFilter() const = 0;
    virtual std::string_view assetFileExtension() const = 0;

    virtual bool isAssetMetaFile(const std::string& assetFilePath) = 0;
    virtual bool isAssetValid(const std::string& assetFilePath) = 0;

    virtual std::vector<std::string> getAssetPrimsPath(const std::string& assetPath) = 0;

    virtual std::shared_ptr<NauProjectBrowserItemTypeResolverInterface> createTypeResolver() = 0;
};


// ** NauAssetManagerInterface

using NauAssetTypesList = std::vector<NauEditorFileType>;

class NAU_EDITOR_API NauAssetManagerInterface
{
public:
    virtual ~NauAssetManagerInterface() = default;

    virtual void initialize(const NauProject& project) = 0;

    virtual void importAsset(const std::string& sourcePath) = 0;

    virtual std::shared_ptr<NauProjectBrowserItemTypeResolverInterface> typeResolver() = 0;

    virtual std::string sourcePathFromAsset(const std::string& assetPath) = 0;

    virtual std::string_view assetFileFilter() const = 0;
    virtual std::string_view assetFileExtension() const = 0;

    virtual void addClient(const NauAssetTypesList& types, NauAssetManagerClientInterface* client) = 0;
};

using NauAssetManagerInterfacePtr = std::shared_ptr<NauAssetManagerInterface>;