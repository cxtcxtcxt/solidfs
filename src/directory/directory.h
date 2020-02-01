#pragma once

#include "common.h"
#include <unordered_map>
#include <string>
#include <map>

class Directory {
    // unordered_map is more efficient for pure entity look up
    //std::unordered_map<std::string, int> entry_m;
    iid_t id;
    std::unordered_map<std::string, iid_t> entry_m;

    public:
    Directory(iid_t id, iid_t parent);
    int insert_entry(const std::string& s,iid_t id);
    int remove_entry(const std::string& s);
    int contain_entry(const std::string& s) const;

    int serialize(uint8_t* byte_stream);
    int deserialize(const uint8_t* byte_stream);
};