#pragma once

#include <string>
#include <vector>
#include "common.h"
#include "utils/log_utils.h"
#include "inode/inode_manager.h"
#include "block/block_manager.h"
#include "directory/directory.h"

//TODO(lonhh) when should we update the inode?
class FileSystem {
  //TODO(lonhh): just for debug
public:
    INodeManager* im;
    BlockManager* bm;
    Storage* storage;

public:
    // just used for DEBUG
    FileSystem() {};
    FileSystem(bid_t nr_blocks,bid_t nr_iblock_blocks);
    void mkfs();

    int read(iid_t id,uint8_t* dst,uint32_t size,uint32_t offset);
    int write(iid_t id,const uint8_t* src,uint32_t size,uint32_t offset);
    int truncate(iid_t id, uint32_t size);
    
    // todo
    int unlink(iid_t id);

    int path2iid(const std::string& path,iid_t* id);
    Directory read_directory(iid_t id);
    int write_directory(iid_t id,Directory& dr);

    std::vector<std::string> parse_path(const std::string& path);

//private:
    // allocate a new datablock for inode, but shall we see the index of datablocks? yes we can
    // most of the time we should write the file immediately after allocating a new block for it
    bid_t new_dblock(INode& inode);

    // we don't modify the file size
    int delete_dblock(INode& inode);
    // notice we only allocate a new inode, but we need to write it/init it
    iid_t new_inode(const std::string& file_name,INode& inode);

    Directory read_directory(INode& inode);
    int write_directory(INode& inode,Directory& dr);

    std::vector<bid_t> read_dblock_index(INode& inode,uint32_t begin,uint32_t end);
    uint32_t block_lookup_per_region(INode& inode,uint32_t begin,uint32_t end,std::vector<bid_t>& vec,int depth);
};