#pragma once
#include "pack_file_transaction.h"

namespace dingosdk::anim_studio {
// A saved trick's metadata and clip are one edit. Replacing either alone can corrupt an existing save.
inline void write_saved_trick_files(const std::filesystem::path& folder, const std::string& name,
                                    std::string_view json, std::string_view clip) {
    const std::filesystem::path leaf(name);
    if (name.empty() || leaf.has_parent_path() || leaf.has_root_path() || leaf == L"." || leaf == L".." || name.find(':') != std::string::npos)
        throw std::runtime_error("Invalid saved trick name");
    PackFileTransaction transaction(folder);
    transaction.write(folder / leaf / (name + ".clip"), clip);
    transaction.write(folder / leaf / (name + ".json"), json);
    transaction.commit();
}
}
