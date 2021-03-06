/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *            Copyright (C) 2018 University of Chinese Academy of Sciences, UCAS
 *               Author : Chen Canyu (email : chencanyu@mails.ucas.ac.cn)		
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *                          the file system part of the whole OS
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this 
 * software and associated documentation files (the "Software"), to deal in the Software 
 * without restriction, including without limitation the rights to use, copy, modify, 
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit 
 * persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE. 
 * 
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * */

#include "fs.h"
#include "time.h"
/*
* SD card file system for OS seminar
* This filesystem looks like this:
* FS size : 1GB
* 1 Block : 4KB, total Blocks : 256K
* 1 Inode : 128B
* Inode Bitmap size : 8K / 8 * 1B= 1KB
* Block Bitmap size : 256K / 8 * 1B= 32KB
* --------------------------------------------------------------------------------
* | Superblock   | Block Bitmap  | Inode Bitmap  | Inode Table    |  Blocks    |
* | 1 Block 4KB  | 8 Blocks 32KB | 1 Block  4KB  | 256 Blocks 1MB |  Others    |
* --------------------------------------------------------------------------------
*/

uint8_t superblock_buffer[BLOCK_SIZE] = {0};
uint8_t blockbmp_buffer[BLOCK_BITMAP_SIZE] = {0};
uint8_t inodebmp_block_buffer[BLOCK_SIZE] = {0};
uint8_t inodetable_block_buffer[BLOCK_SIZE] = {0};

uint8_t data_block_buffer[BLOCK_SIZE] = {0};
uint8_t dentry_block_buffer[BLOCK_SIZE] = {0};

file_descriptor_t file_descriptor_table[MAX_FILE_DESCRIPTOR_NUM];

superblock_t *superblock_ptr = (superblock_t *)superblock_buffer;

uint8_t find_file_buffer[BLOCK_SIZE] = {0};
uint8_t parse_file_buffer[MAX_PATH_LENGTH] = {0};

uint32_t buffer1[POINTER_PER_BLOCK] = {0};
uint32_t buffer2[POINTER_PER_BLOCK] = {0};
uint32_t buffer3[POINTER_PER_BLOCK] = {0};
uint32_t buffer0[POINTER_PER_BLOCK] = {0};

inode_t root_inode;
inode_t *root_inode_ptr = &root_inode;

inode_t current_dir;
inode_t *current_dir_ptr = &current_dir;

inode_t current_dir_link;
inode_t *current_dir_link_ptr = &current_dir_link;

uint8_t cat_buffer[CAT_MAX_LENGTH] = {0};

uint8_t fread_buffer[FILE_READ_MAX_LENGTH];
uint8_t fwrite_buffer[FILE_WRITE_MAX_LENGTH];

char parent_buffer[MAX_PATH_LENGTH];
char parent_buffer_1[MAX_PATH_LENGTH];
char parent_buffer_2[MAX_PATH_LENGTH];
char path_buffer[MAX_PATH_LENGTH];
char name_buffer[MAX_NAME_LENGTH];

dentry_t ls_buffer[MAX_LS_NUM] = {0};

// static uint32_t flag_first_write = 0;

static void set_block_bmp(uint32_t block_index)
{
    set_bitmap((BitMap_t)blockbmp_buffer, block_index);        
    return;
}

static void unset_block_bmp(uint32_t block_index)
{
    unset_bitmap((BitMap_t)blockbmp_buffer, block_index);
    return;
}

static bool_t check_block_bmp(uint32_t block_index)
{
    return check_bitmap((BitMap_t)blockbmp_buffer, block_index);
}

static void set_inode_bmp(uint32_t inum)
{
    set_bitmap((BitMap_t)inodebmp_block_buffer, inum);        
    return;   
}

static void unset_inode_bmp(uint32_t inum)
{
    unset_bitmap((BitMap_t)inodebmp_block_buffer, inum);
    return;   
}

static bool_t check_inode_bmp(uint32_t inum)
{
    return check_bitmap((BitMap_t)inodebmp_block_buffer, inum);
}

//-------------------------------------------------------------------------------

void sd_card_read(void *dest, uint32_t sd_offset, uint32_t size)
{
    sdread((char *)dest, sd_offset, size);
}

void sd_card_write(void *dest, uint32_t sd_offset, uint32_t size)
{
    sdwrite((char *)dest, sd_offset, size);
}

//only write_block(), and read_block() interact with SD card
//other func interact with buffer in memory
static void write_block(uint32_t block_index, uint8_t *block_buffer)
{
    uint32_t sd_offset = block_index*BLOCK_SIZE + FS_START_SD_OFFSET;
    sd_card_write(block_buffer, sd_offset, BLOCK_SIZE);
}

static void read_block(uint32_t block_index, uint8_t *block_buffer)
{
    uint32_t sd_offset = block_index*BLOCK_SIZE + FS_START_SD_OFFSET;
    sd_card_read(block_buffer, sd_offset, BLOCK_SIZE);
}

//sync from memory to disk
static void sync_to_disk_inode_bmp()
{
    write_block(INODE_BMP_BLOCK_INDEX, inodebmp_block_buffer);
}

static void sync_to_disk_block_bmp()
{
    int i = 0;
    for(; i < BLOCK_BMP_BLOCKS_NUM; i++){
        write_block(BLOCK_BMP_BLOCK_INDEX + i, blockbmp_buffer + BLOCK_SIZE * i);
    }
    return;
}

static void sync_to_disk_superblock()
{
    write_block(SUPERBLOCK_BLOCK_INDEX, superblock_buffer);
}

static void sync_to_disk_inode_table(uint32_t inode_table_offset)
{
    write_block(INODE_TABLE_BLOCK_INDEX + inode_table_offset, inodetable_block_buffer);
}

static void sync_to_disk_file_data(uint32_t block_index)
{
    write_block(block_index, data_block_buffer);
}

static void sync_to_disk_dentry(uint32_t block_index)
{
    write_block(block_index, dentry_block_buffer);
}

//sync from disk to memory
static void sync_from_disk_inode_bmp()
{
    read_block(INODE_BMP_BLOCK_INDEX, inodebmp_block_buffer);
}

static void sync_from_disk_block_bmp()
{
    int i = 0;
    for(; i < BLOCK_BMP_BLOCKS_NUM; i++){
        read_block(BLOCK_BMP_BLOCK_INDEX + i, blockbmp_buffer + BLOCK_SIZE * i);
    }
    return;
}

static void sync_from_disk_superblock()
{
    read_block(SUPERBLOCK_BLOCK_INDEX, superblock_buffer);
}

static void sync_from_disk_inode_table(uint32_t inode_table_offset)
{
    read_block(INODE_TABLE_BLOCK_INDEX + inode_table_offset, inodetable_block_buffer);
}

static void sync_from_disk_file_data(uint32_t block_index)
{
    read_block(block_index, data_block_buffer);
}

static void sync_from_disk_dentry(uint32_t block_index)
{
    read_block(block_index, dentry_block_buffer);
}

// static bool_t count_char_in_string(char c, char *str)
static int count_char_in_string(char c, char *str)
{
    int i = 0, cnt = 0;
    while(str[i] != '\0'){
        if(str[i] == c){
            cnt++;
        }
        i++;
    }
    return cnt;
}

//------------------------------------------------------------------------------------------

static void clear_block_index(uint32_t block_index)
{
    bzero(buffer0, POINTER_PER_BLOCK*sizeof(uint32_t));
    write_block(block_index, (uint8_t *)buffer0);
}

static void write_to_buffer_inode(inode_t *inode_ptr)
{
    memcpy((inodetable_block_buffer + (inode_ptr->i_num % INODE_NUM_PER_BLOCK)*INODE_SIZE), 
           (uint8_t *)inode_ptr, INODE_SIZE);
}

static void sync_from_disk_inode(uint32_t inum, inode_t *inode_ptr)
{
    uint32_t inode_table_offset = inum / INODE_NUM_PER_BLOCK;
    sync_from_disk_inode_table(inode_table_offset);
    memcpy((uint8_t *)inode_ptr,inodetable_block_buffer+(inum%INODE_NUM_PER_BLOCK)*INODE_SIZE, INODE_SIZE);
}

static void sync_to_disk_inode(inode_t *inode_ptr)
{
    bzero(inodetable_block_buffer, BLOCK_SIZE);
    uint32_t inode_table_offset = inode_ptr->i_num / INODE_NUM_PER_BLOCK;
    sync_from_disk_inode_table(inode_table_offset);
    write_to_buffer_inode(inode_ptr);
    // sync_from_disk_inode_table(inode_table_offset);
    //BUG!!!!!!!!!!!!!
    sync_to_disk_inode_table(inode_table_offset);
}

static int write_dentry(inode_t* inode_ptr, uint32_t dnum, dentry_t* dentry_ptr)
{
    if(dnum < 2){
        vt100_move_cursor(1, 45);
        printk("[FS ERROR] ERROR_DIR_NUM_INCORRECT\n");
        return ERROR_DIR_NUM_INCORRECT;
    }
    if(dnum > inode_ptr->i_fnum + 2){
        vt100_move_cursor(1, 45);
        printk("[FS ERROR] ERROR_DENTRY_SETTING_INCORRECT\n");
        return ERROR_DENTRY_SETTING_INCORRECT;
    }

    uint32_t major_index, minor_index;
    major_index = dnum / DENTRY_NUM_PER_BLOCK;
    minor_index = dnum % DENTRY_NUM_PER_BLOCK;

    bzero(dentry_block_buffer, BLOCK_SIZE);
    dentry_t *dentry_table = (dentry_t *)dentry_block_buffer;

    if(get_block_index_in_inode(inode_ptr, major_index) == 0){
        uint32_t free_block_index = find_free_block();
        set_block_bmp(free_block_index);
        sync_to_disk_block_bmp();

        superblock_ptr->s_free_blocks_cnt--;
        sync_to_disk_superblock();

        write_block_index_in_inode(inode_ptr, major_index, free_block_index);
        inode_ptr->i_fsize += BLOCK_SIZE;
        inode_ptr->i_fnum++;
        sync_to_disk_inode(inode_ptr);
    }
    else{
        inode_ptr->i_fnum++;
        sync_to_disk_inode(inode_ptr);

        read_block(get_block_index_in_inode(inode_ptr, major_index), dentry_block_buffer);
    }
    memcpy((uint8_t *)(&(dentry_table[minor_index])), (uint8_t *)dentry_ptr, DENTRY_SIZE);
    write_block(get_block_index_in_inode(inode_ptr, major_index), dentry_block_buffer);

    return;
}

static int remove_dentry(inode_t* inode_ptr, uint32_t dnum)
{
    dentry_t den;
    bzero(&den, sizeof(dentry_t));
    // read_dentry(inode_ptr, inode_ptr->i_fnum + 1, &last_dentry);
    write_dentry(inode_ptr, dnum, &den);
    inode_ptr->i_fnum--;
    sync_to_disk_inode(inode_ptr);
    return;
}

void read_dentry(inode_t* inode_ptr, uint32_t dnum, dentry_t* dentry_ptr)
{
    uint32_t major_loc, minor_loc;
    major_loc = dnum / DENTRY_NUM_PER_BLOCK;
    minor_loc = dnum % DENTRY_NUM_PER_BLOCK;
    bzero(find_file_buffer, BLOCK_SIZE);
    dentry_t *den_tmp =(dentry_t *)find_file_buffer;
    read_block(get_block_index_in_inode(inode_ptr, major_loc), find_file_buffer);
    memcpy((void*)dentry_ptr, (void*)&(den_tmp[minor_loc]), sizeof(dentry_t));
    return;
}

//----------------------------------------------------------------------------------------

void separate_path(const char *path, char *parent, char *name)
{
    strcpy(parent, (char *)path);
    char* loc = strrchr(parent, '/');
    strcpy(name, loc + 1);
    // name[MAX_NAME_LENGTH - 1] = '\0';
    name[strlen(loc + 1)] = '\0';
    if(loc == parent){
        loc++;
    } 
    *loc = '\0';
    return;
}

int get_block_index_in_inode(inode_t *inode_ptr, uint32_t idx)
{
    bzero(buffer1, POINTER_PER_BLOCK*sizeof(uint32_t));
    bzero(buffer2, POINTER_PER_BLOCK*sizeof(uint32_t));
    bzero(buffer3, POINTER_PER_BLOCK*sizeof(uint32_t));
    
    if(idx < FIRST_POINTER){
        return inode_ptr->i_direct_table[idx];
    }
    if(idx < SECOND_POINTER){
        if(inode_ptr->i_indirect_block_1_ptr == 0){
            return -1;
        }
        read_block(inode_ptr->i_indirect_block_1_ptr, (uint8_t *)buffer1);
        return buffer1[idx - FIRST_POINTER];
    }
    if(idx < THIRD_POINTER){
        if(inode_ptr->i_indirect_block_1_ptr == 0){
            return -1;
        }
        read_block(inode_ptr->i_indirect_block_1_ptr, (uint8_t *)buffer1);
        if(buffer1[(idx - SECOND_POINTER) / POINTER_PER_BLOCK] == 0){
            return -1;
        }
        read_block(buffer1[(idx - SECOND_POINTER) / POINTER_PER_BLOCK], (uint8_t *)buffer2);
        return buffer2[(idx - SECOND_POINTER) % POINTER_PER_BLOCK];
    }
    if(idx < MAX_BLOCK_INDEX){
        if(inode_ptr->i_indirect_block_1_ptr == 0){
            return -1;
        }
        read_block(inode_ptr->i_indirect_block_1_ptr, (uint8_t *)buffer1);
        if(buffer1[(idx - THIRD_POINTER) / (POINTER_PER_BLOCK * POINTER_PER_BLOCK)] == 0){
            return -1;
        } 
        read_block(buffer1[(idx - THIRD_POINTER) / (POINTER_PER_BLOCK * POINTER_PER_BLOCK)], (uint8_t *)buffer2);
        if(buffer2[((idx - THIRD_POINTER) % (POINTER_PER_BLOCK * POINTER_PER_BLOCK)) / POINTER_PER_BLOCK] == 0){
            return -1;
        } 
        read_block(buffer2[((idx - THIRD_POINTER) % (POINTER_PER_BLOCK * POINTER_PER_BLOCK)) / POINTER_PER_BLOCK], (uint8_t *)buffer3);
        return buffer3[(idx - THIRD_POINTER) % POINTER_PER_BLOCK];
    }   
}

void write_block_index_in_inode(inode_t *inode_ptr, uint32_t idx, uint32_t block_index)
{
    bzero(buffer1, POINTER_PER_BLOCK*sizeof(uint32_t));
    bzero(buffer2, POINTER_PER_BLOCK*sizeof(uint32_t));
    bzero(buffer3, POINTER_PER_BLOCK*sizeof(uint32_t));

    if(idx < FIRST_POINTER){
        inode_ptr->i_direct_table[idx] = block_index;
        sync_to_disk_inode(inode_ptr);
        return;
    }

    uint32_t free_index_1;
    if(idx < SECOND_POINTER){
        if(inode_ptr->i_indirect_block_1_ptr == 0){
            free_index_1 = find_free_block();

            set_block_bmp(free_index_1);
            sync_to_disk_block_bmp();

            superblock_ptr->s_free_blocks_cnt--;
            sync_to_disk_superblock();

            clear_block_index(free_index_1);
            inode_ptr->i_indirect_block_1_ptr = free_index_1;
            inode_ptr->i_fsize += BLOCK_SIZE;
            sync_to_disk_inode(inode_ptr);
        }
        read_block(inode_ptr->i_indirect_block_1_ptr, (uint8_t *)buffer1);
        buffer1[idx - FIRST_POINTER] = block_index;
        write_block(inode_ptr->i_indirect_block_1_ptr, (uint8_t *)buffer1);
        return;
    }

    uint32_t free_index_2;
    if(idx < THIRD_POINTER){
        if(inode_ptr->i_indirect_block_2_ptr == 0){
            free_index_1 = find_free_block();

            set_block_bmp(free_index_1);
            sync_to_disk_block_bmp();

            superblock_ptr->s_free_blocks_cnt--;
            sync_to_disk_superblock();

            clear_block_index(free_index_1);
            inode_ptr->i_indirect_block_2_ptr = free_index_1;
            inode_ptr->i_fsize += BLOCK_SIZE;
            sync_to_disk_inode(inode_ptr);
        }
        read_block(inode_ptr->i_indirect_block_2_ptr, (uint8_t *)buffer1);
        if(buffer1[(idx - SECOND_POINTER) / POINTER_PER_BLOCK] == 0){
            free_index_2 = find_free_block();

            set_block_bmp(free_index_2);
            sync_to_disk_block_bmp();

            superblock_ptr->s_free_blocks_cnt--;
            sync_to_disk_superblock();

            clear_block_index(free_index_2);
            inode_ptr->i_fsize += BLOCK_SIZE;
            sync_to_disk_inode(inode_ptr);

            buffer1[(idx - SECOND_POINTER) / POINTER_PER_BLOCK] = free_index_2;
            write_block(inode_ptr->i_indirect_block_2_ptr, (uint8_t *)buffer1);
        }
        read_block(buffer1[(idx - SECOND_POINTER) / POINTER_PER_BLOCK], (uint8_t *)buffer2);
        buffer2[(idx - SECOND_POINTER) % POINTER_PER_BLOCK] = block_index;
        write_block(buffer1[(idx - SECOND_POINTER) / POINTER_PER_BLOCK], (uint8_t *)buffer2);
    }

    uint32_t free_index_3;
    if(idx < MAX_BLOCK_INDEX){
        if(inode_ptr->i_indirect_block_3_ptr == 0){
            free_index_1 = find_free_block();

            set_block_bmp(free_index_1);
            sync_to_disk_block_bmp();

            superblock_ptr->s_free_blocks_cnt--;
            sync_to_disk_superblock();

            clear_block_index(free_index_1);
            inode_ptr->i_indirect_block_3_ptr = free_index_1;
            inode_ptr->i_fsize += BLOCK_SIZE;
            sync_to_disk_inode(inode_ptr);
        }
        read_block(inode_ptr->i_indirect_block_3_ptr, (uint8_t *)buffer1);
        if(buffer1[(idx - THIRD_POINTER) / (POINTER_PER_BLOCK * POINTER_PER_BLOCK)] == 0){
            free_index_2 = find_free_block();

            set_block_bmp(free_index_2);
            sync_to_disk_block_bmp();

            superblock_ptr->s_free_blocks_cnt--;
            sync_to_disk_superblock();

            clear_block_index(free_index_2);
            inode_ptr->i_fsize += BLOCK_SIZE;
            sync_to_disk_inode(inode_ptr);

            buffer1[(idx - THIRD_POINTER) / (POINTER_PER_BLOCK * POINTER_PER_BLOCK)] = free_index_2;
            write_block(inode_ptr->i_indirect_block_2_ptr, (uint8_t *)buffer1);
        }
        read_block(buffer1[(idx - THIRD_POINTER) / (POINTER_PER_BLOCK * POINTER_PER_BLOCK)], (uint8_t *)buffer2);
        if(buffer2[((idx - THIRD_POINTER) % (POINTER_PER_BLOCK * POINTER_PER_BLOCK)) / POINTER_PER_BLOCK] == 0){
            free_index_3 = find_free_block();

            set_block_bmp(free_index_3);
            sync_to_disk_block_bmp();

            superblock_ptr->s_free_blocks_cnt--;
            sync_to_disk_superblock();

            clear_block_index(free_index_3);
            inode_ptr->i_fsize += BLOCK_SIZE;
            sync_to_disk_inode(inode_ptr);

            buffer2[((idx - THIRD_POINTER) % (POINTER_PER_BLOCK * POINTER_PER_BLOCK)) / POINTER_PER_BLOCK] = free_index_3;
            write_block(buffer1[(idx - THIRD_POINTER) / (POINTER_PER_BLOCK * POINTER_PER_BLOCK)], (uint8_t *)buffer2);
        }
        read_block(buffer2[((idx - THIRD_POINTER) % (POINTER_PER_BLOCK * POINTER_PER_BLOCK)) / POINTER_PER_BLOCK], (uint8_t *)buffer3);
        buffer3[(idx - THIRD_POINTER) % POINTER_PER_BLOCK] = block_index;
        write_block(buffer2[((idx - THIRD_POINTER) % (POINTER_PER_BLOCK * POINTER_PER_BLOCK)) / POINTER_PER_BLOCK], (uint8_t *)buffer3);
        return;
    }
    return;
}

// int find_file(inode_t *inode_ptr, const char *name)
int find_file(inode_t *inode_ptr, char *name)
{
    bzero(find_file_buffer, BLOCK_SIZE);
    dentry_t *p = (dentry_t *)find_file_buffer;
    int i = 0, j = 0;
    for(; i < MAX_BLOCK_INDEX; i++){
        uint32_t block_index = get_block_index_in_inode(inode_ptr, i);
        read_block(block_index, find_file_buffer);

        // for(; j < POINTER_PER_BLOCK; j++){
        for(; j < DENTRY_NUM_PER_BLOCK; j++){
            if(strcmp((char *)name, p[j].d_name) == 0){

                // //debug
                // vt100_move_cursor(1, 26);
                // printk("[DEBUG 1 find_file] strcmp:%d, j:%d, block_index:%d, p[i].d_name:%s", \
                //         strcmp((char *)name, p[j].d_name), j, block_index, p[i].d_name);
                // printk(", name:%s", name);
                
                return p[j].d_inum;
            }
        }
    }
    return -1;
}

static int _find_file(inode_t *inode_ptr, char *name)
{
    bzero(find_file_buffer, BLOCK_SIZE);
    dentry_t *p = (dentry_t *)find_file_buffer;
    int i = 0, j = 0;
    for(; i < 2; i++){
        uint32_t block_index = get_block_index_in_inode(inode_ptr, i);
        read_block(block_index, find_file_buffer);

        // for(; j < POINTER_PER_BLOCK; j++){
        for(; j < DENTRY_NUM_PER_BLOCK; j++){
            if(strcmp((char *)name, p[j].d_name) == 0){

                // //debug
                // vt100_move_cursor(1, 26);
                // printk("[DEBUG 1 find_file] strcmp:%d, j:%d, block_index:%d, p[i].d_name:%s", \
                //         strcmp((char *)name, p[j].d_name), j, block_index, p[i].d_name);
                // printk(", name:%s", name);
                
                return p[j].d_inum;
            }
        }
    }
    return -1;
}

int find_dentry(inode_t* inode_ptr, const char* name) {
    bzero(find_file_buffer, BLOCK_SIZE);
    dentry_t *p = (dentry_t *)find_file_buffer;
    int i = 0, j = 0;
    // for(; i < MAX_BLOCK_INDEX; i++){
    //MAX_BLOCK_INDEX is TOO BIG!!!!!!!!
    // for(; i < FIRST_POINTER; i++){
    for(; i < MAX_DENTRY_BLOCK_NUM; i++){
        uint32_t block_index = get_block_index_in_inode(inode_ptr, i);
        read_block(block_index, find_file_buffer);
        // for(; j < POINTER_PER_BLOCK; j++){
        for(; j < DENTRY_NUM_PER_BLOCK; j++){
            if(strcmp((char *)name, p[j].d_name) == 0){
                return (i * DENTRY_NUM_PER_BLOCK + j);     
            }
        }
    }
    return -1;
}

// uint32_t parse_path(const char *path, inode_t *inode_ptr)
// {
//     return find_file(inode_ptr, (char *)path);
// }

uint32_t parse_path(const char *path, inode_t *inode_ptr)
{
    bzero(parse_file_buffer, MAX_PATH_LENGTH);
/*
    char *p = "./";
    char *p_ = "/";
    strcpy(parse_file_buffer, p);
    strcpy(parse_file_buffer+2, (char *)path);
    strcpy(parse_file_buffer+strlen(parse_file_buffer), p_);
*/
    // char *p = "./";
    char *p_ = "/";
    // strcpy(parse_file_buffer, p);
    // strcpy(parse_file_buffer+2, (char *)path);
    strcpy(parse_file_buffer, (char *)path);

    strcpy(parse_file_buffer+strlen(parse_file_buffer), p_);

    int i = 0;
    char *_p = &parse_file_buffer[0];

    inode_t _inode;
    uint32_t inum;
    memcpy((uint8_t *)&_inode, (uint8_t *)inode_ptr, INODE_SIZE);

    uint32_t l = strlen(parse_file_buffer);

    for(; i < l; i++){
        if(parse_file_buffer[i] == '/'){
            parse_file_buffer[i] = '\0';

            inum = find_file(&_inode, _p);
            sync_from_disk_inode(inum, &_inode);

            _p = &parse_file_buffer[i+1];

        }
    }
    return inum;
}

int find_free_inode()
{
    int i = 0, j = 0;
    for(; i < INODE_BITMAP_SIZE; i++){        

        if(inodebmp_block_buffer[i] != 0xff){
            while(check_inode_bmp(j) == 1){
                j++;
            }
            return j;
        }
        else{
            j += BYTE_SIZE;         
        }
    }
    vt100_move_cursor(1, 45);
    printk("[FS ERROR] ERROR_NO_FREE_INODE\n");
    return ERROR_NO_FREE_INODE;
}

int find_free_block()
{
    int i = 0, j = 0;
    for(; i < BLOCK_BITMAP_SIZE; i++){
        if(blockbmp_buffer[i] != 0xff){
            while(check_block_bmp(j) == 1){
                j++;
            }
            return j;
        }
        else{
            j += BYTE_SIZE;         
        }
    }
    vt100_move_cursor(1, 45);
    printk("[FS ERROR] ERROR_NO_FREE_BLOCK\n");
    return ERROR_NO_FREE_BLOCK;
}

void release_inode_block(inode_t *inode_ptr)
{
    uint32_t i, j, k;
    bzero(buffer1, POINTER_PER_BLOCK*sizeof(uint32_t));
    bzero(buffer2, POINTER_PER_BLOCK*sizeof(uint32_t));
    bzero(buffer3, POINTER_PER_BLOCK*sizeof(uint32_t));

    for(i = 0; i < FIRST_POINTER; i++){
        if(inode_ptr->i_direct_table[i] == 0){
            return;
        }
        unset_block_bmp(inode_ptr->i_direct_table[i]);
    }

    if(inode_ptr->i_indirect_block_1_ptr == 0){
        return;
    }
    read_block(inode_ptr->i_indirect_block_1_ptr, (uint8_t *)buffer1);
    for(i = 0; i < POINTER_PER_BLOCK; i++){
        if(buffer1[i] == 0){
            unset_block_bmp(inode_ptr->i_indirect_block_1_ptr);
            return;
        }
        unset_block_bmp(buffer1[i]);
    }
    unset_block_bmp(inode_ptr->i_indirect_block_1_ptr);

    if(inode_ptr->i_indirect_block_2_ptr == 0){
        return;
    }
    read_block(inode_ptr->i_indirect_block_2_ptr, (uint8_t *)buffer1);
    for(i = 0; i < POINTER_PER_BLOCK; i++){
        if(buffer1[i] == 0){
            unset_block_bmp(inode_ptr->i_indirect_block_2_ptr);
            return;
        }
        read_block(buffer1[i], (uint8_t *)buffer2);
        for(j = 0; j < POINTER_PER_BLOCK; j++){
            if(buffer2[j] == 0){
                unset_block_bmp(buffer1[i]);
                unset_block_bmp(inode_ptr->i_indirect_block_2_ptr);
                return;
            }
            unset_block_bmp(buffer2[j]);
        }
        unset_block_bmp(buffer1[i]);
    }
    unset_block_bmp(inode_ptr->i_indirect_block_2_ptr);

    if(inode_ptr->i_indirect_block_3_ptr == 0){
        return;
    }
    read_block(inode_ptr->i_indirect_block_3_ptr, (uint8_t *)buffer1);
    for(i = 0; i < POINTER_PER_BLOCK; i++){
        if(buffer1[i] == 0){
            unset_block_bmp(inode_ptr->i_indirect_block_3_ptr);
            return;
        }
        read_block(buffer1[i], (uint8_t *)buffer2);
        for(j = 0; j < POINTER_PER_BLOCK; j++){
            if(buffer2[j] == 0){
                unset_block_bmp(buffer1[i]);
                unset_block_bmp(inode_ptr->i_indirect_block_3_ptr);
                return;
            }
            read_block(buffer2[j], (uint8_t *)buffer3);
            for(k = 0; k < POINTER_PER_BLOCK; k++){
                if(buffer3[k] == 0){
                    unset_block_bmp(buffer2[j]);
                    unset_block_bmp(buffer1[i]);
                    unset_block_bmp(inode_ptr->i_indirect_block_3_ptr);
                    return;
                }
                unset_block_bmp(buffer3[k]);
            }
            unset_block_bmp(buffer2[j]);
        }
        unset_block_bmp(buffer1[i]);
    }
    unset_block_bmp(inode_ptr->i_indirect_block_3_ptr);
    return;
}

int is_empty_dnetry(dentry_t *dentry_ptr)
{
    return ((dentry_ptr->d_inum == 0) && (dentry_ptr->d_name[0] == '\0'));
}

//---------------------------------FILE SYSTEM OPERATIONS-----------------------------------------

//operations on file system
void init_fs()
{
    sync_from_disk_superblock();

    if(superblock_ptr->s_magic == FS_MAGIC_NUMBER){

        sync_from_disk_block_bmp();
        sync_from_disk_inode_bmp();

        sync_from_disk_inode(0, root_inode_ptr);
        // memcpy((uint8_t *)&current_dir, (uint8_t *)&root_inode, sizeof(dentry_t));
        memcpy((uint8_t *)&current_dir, (uint8_t *)&root_inode, sizeof(inode_t));
        // current_dir_ptr = root_inode_ptr;

        vt100_move_cursor(1, 1);    
        printk("[FS] File system exists in the disk!      \n");
        printk("[FS] File system current informatin:      \n");
        printk("     magic number : 0x%x                  \n", superblock_ptr->s_magic);
        printk("     file system size : 0x%x              \n", superblock_ptr->s_disk_size);
        printk("     block size : 0x%x                    \n", superblock_ptr->s_block_size);
        printk("     total blocks : %d                    \n", superblock_ptr->s_total_blocks_cnt);
        printk("     free blocks : %d                     \n", superblock_ptr->s_free_blocks_cnt);
        printk("     total inodes : %d                    \n", superblock_ptr->s_total_inodes_cnt);
        printk("     free inodes : %d                     \n", superblock_ptr->s_free_inode_cnt);
        printk("     block bitmap start-block index : %d  \n", superblock_ptr->s_blockbmp_block_index);
        printk("                  disk offset : %d        \n", superblock_ptr->s_blockbmp_block_index * BLOCK_SIZE);
        printk("     inode bitmap start-block index : %d  \n", superblock_ptr->s_inodebmp_block_index);
        printk("                  disk offset : %d        \n", superblock_ptr->s_inodebmp_block_index * BLOCK_SIZE);
        printk("     inode table start-block index : %d   \n", superblock_ptr->s_inodetable_block_index);
        printk("                 disk offset : %d         \n", superblock_ptr->s_inodetable_block_index * BLOCK_SIZE);
        printk("     file data start-block index : %d     \n", superblock_ptr->s_data_block_index);
        printk("     inode entry size : %d                \n", superblock_ptr->s_inode_size);
        printk("     dir entry size : %d                  \n", superblock_ptr->s_dentry_size);
    }
    else{
        do_mkfs();
    }
}

void do_mkfs()
{
    bzero(data_block_buffer, BLOCK_SIZE);
    bzero(superblock_buffer, BLOCK_SIZE);
    bzero(blockbmp_buffer, BLOCK_BITMAP_SIZE);
    bzero(inodebmp_block_buffer, BLOCK_SIZE);
    bzero(dentry_block_buffer, BLOCK_SIZE);
    bzero(inodetable_block_buffer, BLOCK_SIZE);

    bzero(file_descriptor_table, sizeof(file_descriptor_t)*MAX_FILE_DESCRIPTOR_NUM);

    bzero(parent_buffer, MAX_PATH_LENGTH);
    bzero(name_buffer, MAX_NAME_LENGTH);

    sync_to_disk_block_bmp();
    sync_to_disk_inode_bmp();

    //clear_disk();

    superblock_ptr->s_magic = FS_MAGIC_NUMBER;
    superblock_ptr->s_disk_size = FS_SIZE;
    superblock_ptr->s_block_size = BLOCK_SIZE;
    superblock_ptr->s_total_blocks_cnt = BLOCK_NUM;
    superblock_ptr->s_total_inodes_cnt = INODE_NUM;
    superblock_ptr->s_blockbmp_block_index = BLOCK_BMP_BLOCK_INDEX;
    superblock_ptr->s_inodebmp_block_index = INODE_BMP_BLOCK_INDEX;
    superblock_ptr->s_inodetable_block_index = INODE_TABLE_BLOCK_INDEX;
    superblock_ptr->s_data_block_index = DATA_BLOCK_INDEX;
    // superblock_ptr->s_free_blocks_cnt = BLOCK_NUM - superblock_ptr->s_inodetable_block_index;
    superblock_ptr->s_free_blocks_cnt = BLOCK_NUM - DATA_BLOCK_INDEX;
    superblock_ptr->s_free_inode_cnt = INODE_NUM;
    superblock_ptr->s_inode_size = INODE_SIZE;
    superblock_ptr->s_dentry_size = DENTRY_SIZE;
    sync_to_disk_superblock();

    int i = 0;

    for(; i < DATA_BLOCK_INDEX; i++){
        set_block_bmp(i);
    }

    sync_to_disk_block_bmp();

    //init root dir
    uint32_t root_inum = 0;
    set_inode_bmp(root_inum);
    sync_to_disk_inode_bmp();
    superblock_ptr->s_free_inode_cnt--;
    sync_to_disk_superblock();

    set_block_bmp(DATA_BLOCK_INDEX);
    sync_to_disk_block_bmp();
    superblock_ptr->s_free_blocks_cnt--;
    sync_to_disk_superblock();

    root_inode_ptr->i_fmode = (S_IFDIR | 0755);
    root_inode_ptr->i_links_cnt = 1;
    root_inode_ptr->i_fsize = BLOCK_SIZE;
    root_inode_ptr->i_fnum = 0;
    root_inode_ptr->i_atime = get_ticks();
    root_inode_ptr->i_ctime = get_ticks();
    root_inode_ptr->i_mtime = get_ticks();
    bzero(root_inode_ptr->i_direct_table, MAX_DIRECT_NUM*sizeof(uint32_t));
    root_inode_ptr->i_direct_table[0] = DATA_BLOCK_INDEX;
    root_inode_ptr->i_indirect_block_1_ptr = NULL;
    root_inode_ptr->i_indirect_block_2_ptr = NULL;
    root_inode_ptr->i_indirect_block_3_ptr = NULL;
    root_inode_ptr->i_num = 0;
    bzero(root_inode_ptr->padding, 10*sizeof(uint32_t));
    sync_to_disk_inode(root_inode_ptr);

    dentry_t *root_dentry_table = (dentry_t *)dentry_block_buffer;
    root_dentry_table[0].d_inum = 0;
    strcpy(root_dentry_table[0].d_name, ".");
    root_dentry_table[1].d_inum = 0;
    strcpy(root_dentry_table[1].d_name, "..");
    sync_to_disk_dentry(DATA_BLOCK_INDEX);

    // memcpy((uint8_t *)&current_dir, (uint8_t *)&root_inode, sizeof(dentry_t));
    memcpy((uint8_t *)&current_dir, (uint8_t *)&root_inode, sizeof(inode_t));

    vt100_move_cursor(1, 1);    
    printk("[FS] Starting initialize file system!      \n");
    printk("[FS] Setting superblock...                 \n");
    printk("     magic number : 0x%x,                  \n", superblock_ptr->s_magic);
    printk("     file system size : 0x%x,              \n", superblock_ptr->s_disk_size);
    printk("     block size : 0x%x,                    \n", superblock_ptr->s_block_size);
    printk("     total blocks : %d,                    \n", superblock_ptr->s_total_blocks_cnt);
    printk("     total inodes : %d,                    \n", superblock_ptr->s_total_inodes_cnt);
    printk("     block bitmap start-block index : %d,  \n", superblock_ptr->s_blockbmp_block_index);
    printk("                  disk offset : %d,        \n", superblock_ptr->s_blockbmp_block_index * BLOCK_SIZE);
    printk("     inode bitmap start-block index : %d,  \n", superblock_ptr->s_inodebmp_block_index);
    printk("                  disk offset : %d,        \n", superblock_ptr->s_inodebmp_block_index * BLOCK_SIZE);
    printk("     inode table start-block index : %d,   \n", superblock_ptr->s_inodetable_block_index);
    printk("                 disk offset : %d,         \n", superblock_ptr->s_inodetable_block_index * BLOCK_SIZE);
    printk("     file data start-block index : %d,     \n", superblock_ptr->s_data_block_index);
    printk("     inode entry size : %d,                \n", superblock_ptr->s_inode_size);
    printk("     dir entry size : %d,                  \n", superblock_ptr->s_dentry_size);
    printk("[FS] Setting inode bitmap...               \n");
    printk("[FS] Setting block bitmap...               \n");
    printk("[FS] Setting inode table...                \n");
    printk("[FS] Initializing file system finished!    \n");
}

void do_statfs()
{
    sync_from_disk_superblock();
    
    vt100_move_cursor(1, 40);
    printk("[FS] File system current informatin:     \n");
    printk("     magic number : 0x%x                 \n", superblock_ptr->s_magic);
    printk("     file system size : 0x%x             \n", superblock_ptr->s_disk_size);
    printk("     block size : 0x%x                   \n", superblock_ptr->s_block_size);
    printk("     total blocks : %d                   \n", superblock_ptr->s_total_blocks_cnt);
    printk("     free blocks : %d                    \n", superblock_ptr->s_free_blocks_cnt);
    printk("     total inodes : %d                   \n", superblock_ptr->s_total_inodes_cnt);
    printk("     free inodes : %d                    \n", superblock_ptr->s_free_inode_cnt);
    printk("     block bitmap start-block index : %d \n", superblock_ptr->s_blockbmp_block_index);
    printk("                  disk offset : %d       \n", superblock_ptr->s_blockbmp_block_index * BLOCK_SIZE);
    printk("     inode bitmap start-block index : %d \n", superblock_ptr->s_inodebmp_block_index);
    printk("                  disk offset : %d       \n", superblock_ptr->s_inodebmp_block_index * BLOCK_SIZE);
    printk("     inode table start-block index : %d  \n", superblock_ptr->s_inodetable_block_index);
    printk("                 disk offset : %d        \n", superblock_ptr->s_inodetable_block_index * BLOCK_SIZE);
    printk("     file data start-block index : %d    \n", superblock_ptr->s_data_block_index);
    printk("     inode entry size : %d               \n", superblock_ptr->s_inode_size);
    printk("     dir entry size : %d                 \n", superblock_ptr->s_dentry_size);
}

//-----------------------------------DIRECTORY OPERATIONS--------------------------------------------

//operations on directory
uint32_t do_mkdir(const char *path, mode_t mode)
{
    bzero(parent_buffer, MAX_PATH_LENGTH);
    bzero(path_buffer, MAX_PATH_LENGTH);
    bzero(name_buffer, MAX_NAME_LENGTH);

    memcpy((uint8_t *)path_buffer, (uint8_t *)path, strlen((char *)path));
    path_buffer[strlen((char *)path)] = '\0';

    // separate_path(path, parent, name);
    separate_path(path_buffer, parent_buffer, name_buffer);

    uint32_t parent_inum = 0, free_inum, free_block_index;
    // parent_inum = parse_path(parent, current_dir_ptr);
    parent_inum = find_file(current_dir_ptr, parent_buffer);
    
    sync_from_disk_block_bmp();
    sync_from_disk_inode_bmp();

    inode_t parent_inode, new_inode;
    sync_from_disk_inode(parent_inum, &parent_inode);

    if(find_dentry(&parent_inode, name_buffer) != -1){
        vt100_move_cursor(1, 45);
        printk("[FS ERROR] ERROR_DUP_DIR_NAME\n");
        return ERROR_DUP_DIR_NAME;
    }

    free_inum = find_free_inode();
    set_inode_bmp(free_inum);
    sync_to_disk_inode_bmp();

    superblock_ptr->s_free_inode_cnt--;
    sync_to_disk_superblock();

    free_block_index = find_free_block();
    set_block_bmp(free_block_index);
    sync_to_disk_block_bmp();

    superblock_ptr->s_free_blocks_cnt--;
    sync_to_disk_superblock();    

    new_inode.i_fmode = S_IFDIR | mode;
    new_inode.i_links_cnt = 1;
    new_inode.i_fsize = BLOCK_SIZE;
    new_inode.i_fnum = 0;
    new_inode.i_atime = get_ticks();
    new_inode.i_ctime = get_ticks();
    new_inode.i_mtime = get_ticks();
    bzero(new_inode.i_direct_table, MAX_DIRECT_NUM*sizeof(uint32_t));
    new_inode.i_direct_table[0] = free_block_index;
    new_inode.i_indirect_block_1_ptr = NULL;
    new_inode.i_indirect_block_2_ptr = NULL;
    new_inode.i_indirect_block_3_ptr = NULL;
    new_inode.i_num = free_inum;
    bzero(new_inode.padding, 10*sizeof(uint32_t));

    sync_to_disk_inode(&new_inode);

    bzero(dentry_block_buffer, BLOCK_SIZE);
    dentry_t *new_dentry_table = (dentry_t *)dentry_block_buffer;
    new_dentry_table[0].d_inum = free_inum;
    strcpy(new_dentry_table[0].d_name, ".");
    new_dentry_table[1].d_inum = parent_inum;
    strcpy(new_dentry_table[1].d_name, "..");
    sync_to_disk_dentry(free_block_index);

    dentry_t parent_dentry;
    parent_dentry.d_inum = free_inum;
    strcpy(parent_dentry.d_name, name_buffer);
    write_dentry(&parent_inode, parent_inode.i_fnum+2, &parent_dentry);

    return 0;
}

void do_rmdir(const char *path)
{
    sync_from_disk_block_bmp();
    sync_from_disk_inode_bmp();

    bzero(parent_buffer, MAX_PATH_LENGTH);
    bzero(path_buffer, MAX_PATH_LENGTH);
    bzero(name_buffer, MAX_NAME_LENGTH);

    memcpy((uint8_t *)path_buffer, (uint8_t *)path, strlen((char *)path));
    path_buffer[strlen((char *)path)] = '\0';

    // separate_path(path, parent, name);
    separate_path(path_buffer, parent_buffer, name_buffer);

    uint32_t parent_inum = 0;
    // parent_inum = parse_path(parent, current_dir_ptr);
    parent_inum = find_file(current_dir_ptr, parent_buffer);

    inode_t parent_inode;
    sync_from_disk_inode(parent_inum, &parent_inode);

    inode_t child_inode;
    uint32_t child_inum = 0;
    child_inum = find_file(&parent_inode, name_buffer);

    sync_from_disk_inode(child_inum, &child_inode);

    unset_inode_bmp(child_inum);
    sync_to_disk_inode_bmp();

    release_inode_block(&child_inode);
    sync_to_disk_block_bmp();

    uint32_t dnum = find_dentry(&parent_inode, name_buffer);
    remove_dentry(&parent_inode, dnum);
    sync_to_disk_inode(&parent_inode);

    return;
}


void do_ls()
{   
    uint32_t i, j;
    uint32_t k = 0;
    bzero(find_file_buffer, BLOCK_SIZE);
    bzero(ls_buffer, MAX_LS_NUM * sizeof(dentry_t));
    dentry_t* p = (dentry_t *)find_file_buffer;

    bzero(data_block_buffer, BLOCK_SIZE);

    if(S_ISLNK(current_dir_ptr->i_fmode)){
        uint32_t block_index = current_dir_ptr->i_direct_table[0];
        sync_from_disk_file_data(block_index);

        char *_p = ".";
        strcpy(path_buffer, _p);
        strcpy(path_buffer+1, data_block_buffer + sizeof(dentry_t)*2);

        uint32_t inum = parse_path(path_buffer, root_inode_ptr);
        inode_t inode;
        sync_from_disk_inode(inum, &inode);

        memcpy((int8_t *)current_dir_link_ptr, (int8_t *)&inode, sizeof(inode_t));

        for(i = 0;i < MAX_BLOCK_INDEX; i++) {

            // sync_from_disk_inode(0, current_dir_ptr);

            uint32_t block_index = get_block_index_in_inode(current_dir_link_ptr, i);

            read_block(block_index, find_file_buffer);

            for(j = 0; j < DENTRY_NUM_PER_BLOCK;j++) {

                if(!is_empty_dnetry(&p[j])){
                    memcpy((uint8_t *)&ls_buffer[k], (uint8_t *)&p[j], sizeof(dentry_t));
                    k++;
                } 
                else if(is_empty_dnetry(&p[j]) && is_empty_dnetry(&p[j+1]) && is_empty_dnetry(&p[j+2])){
                    return;
                }
            }
        }  

        return;
    }
    else{
        for(i = 0;i < MAX_BLOCK_INDEX; i++) {

            uint32_t block_index = get_block_index_in_inode(current_dir_ptr, i);

            read_block(block_index, find_file_buffer);

            for(j = 0; j < DENTRY_NUM_PER_BLOCK;j++) {

                if(!is_empty_dnetry(&p[j])){
                    memcpy((uint8_t *)&ls_buffer[k], (uint8_t *)&p[j], sizeof(dentry_t));
                    k++;
                } 
                else if(is_empty_dnetry(&p[j]) && is_empty_dnetry(&p[j+1]) && is_empty_dnetry(&p[j+2])){
                    return;
                }
            }
        }

        return;        
    }
}

void do_cd(char *name)
{
    bzero(path_buffer, MAX_PATH_LENGTH);
    bzero(parent_buffer, MAX_PATH_LENGTH);
    bzero(parent_buffer_1, MAX_PATH_LENGTH);
    bzero(parent_buffer_2, MAX_PATH_LENGTH);
    bzero(name_buffer, MAX_NAME_LENGTH);

    memcpy(path_buffer, name, strlen(name));
    parent_buffer[strlen(name)] = '\0';

    char c = '/';

    if(count_char_in_string(c, path_buffer) == 0){
        uint32_t inum;
        if((inum = find_file(current_dir_ptr, name)) != -1){
        // if((inum = find_dentry(current_dir_ptr, name)) != -1){
            inode_t ino;
            sync_from_disk_inode(inum, &ino);
            memcpy((int8_t *)current_dir_ptr, (int8_t *)&ino, sizeof(inode_t));
        }
        return;
    }
    else if(count_char_in_string(c, path_buffer) == 1){
        separate_path(path_buffer, parent_buffer, name_buffer);
        uint32_t inum_1, inum_2;
        if((inum_1 = find_file(current_dir_ptr, parent_buffer)) != -1){
        // if((inum_1 = find_dentry(current_dir_ptr, parent_buffer)) != -1){

            inode_t ino_1;
            sync_from_disk_inode(inum_1, &ino_1);
            memcpy((int8_t *)current_dir_ptr, (int8_t *)&ino_1, sizeof(inode_t));

            if((inum_2 = find_file(current_dir_ptr, name_buffer)) != -1){
            // if((inum_2 = find_dentry(current_dir_ptr, name_buffer)) != -1){

                inode_t ino_2;
                sync_from_disk_inode(inum_2, &ino_2);
                memcpy((int8_t *)current_dir_ptr, (int8_t *)&ino_2, sizeof(inode_t));
            }            
        }
        return;        
    }
    else if(count_char_in_string(c, path_buffer) == 2){
        separate_path(path_buffer, parent_buffer, name_buffer);
        separate_path(parent_buffer, parent_buffer_1, parent_buffer_2);
        uint32_t inum_1, inum_2, inum_3;

        if((inum_1 = find_file(current_dir_ptr, parent_buffer_1)) != -1){

            inode_t ino_1;
            sync_from_disk_inode(inum_1, &ino_1);
            memcpy((int8_t *)current_dir_ptr, (int8_t *)&ino_1, sizeof(inode_t));

            if((inum_2 = find_file(current_dir_ptr, parent_buffer_2)) != -1){

                inode_t ino_2;
                sync_from_disk_inode(inum_2, &ino_2);
                memcpy((int8_t *)current_dir_ptr, (int8_t *)&ino_2, sizeof(inode_t));

                if((inum_3 = find_file(current_dir_ptr, name_buffer)) != -1){

                    inode_t ino_3;
                    sync_from_disk_inode(inum_3, &ino_3);
                    memcpy((int8_t *)current_dir_ptr, (int8_t *)&ino_3, sizeof(inode_t));
                }  
            }            
        }
        return;   
    }
}

//---------------------------------------FILE OPERATIONS---------------------------------------------

static bool_t is_empty_fd(file_descriptor_t *fd_ptr)
{
    return (fd_ptr->fd_inum == 0) && (fd_ptr->fd_mode == 0) && (fd_ptr->fd_r_offset == 0) && (fd_ptr->fd_w_offset == 0);
}

int do_fopen(char *name, uint32_t mode)
{
    bzero(parent_buffer, MAX_PATH_LENGTH);
    bzero(path_buffer, MAX_PATH_LENGTH);
    bzero(name_buffer, MAX_NAME_LENGTH);

    char *p = "./";
    strcpy(path_buffer, p);
    strcpy(path_buffer+2, name);

    // separate_path(path, parent, name);
    separate_path(path_buffer, parent_buffer, name_buffer);

    uint32_t parent_inum = 0;
    // parent_inum = parse_path(parent, current_dir_ptr);
    parent_inum = find_file(current_dir_ptr, parent_buffer);

    inode_t parent_inode;
    sync_from_disk_inode(parent_inum, &parent_inode);

    inode_t child_inode;
    uint32_t child_inum = 0;
    child_inum = find_file(&parent_inode, name_buffer);

    sync_from_disk_inode(child_inum, &child_inode);

    int i = 0;
    for(; i < MAX_FILE_DESCRIPTOR_NUM; i++){
        if(file_descriptor_table[i].fd_inum == child_inum){
            return i;
        }
    }

    int j = 0;
    if(i == MAX_FILE_DESCRIPTOR_NUM){
        while(!is_empty_fd(&file_descriptor_table[j])){
            j++;
        }
        file_descriptor_table[j].fd_inum = child_inum;
        file_descriptor_table[j].fd_mode = mode;
        file_descriptor_table[j].fd_r_offset = 0;
        file_descriptor_table[j].fd_w_offset = 0;
    }

    return j;
}

int do_fwrite(int fd, char *buffer, int length)
{
    bzero(fwrite_buffer, FILE_READ_MAX_LENGTH);

    uint32_t begin_block = file_descriptor_table[fd].fd_w_offset / BLOCK_SIZE;
    uint32_t end_block = (file_descriptor_table[fd].fd_w_offset + length) / BLOCK_SIZE;

    uint32_t block_need = end_block - begin_block;

    inode_t inode;
    sync_from_disk_inode(file_descriptor_table[fd].fd_inum, &inode);

    // if(flag_first_write == 0){
    if(file_descriptor_table[fd].fd_w_offset == 0){
        int i = 0;
        for(; i <= block_need; i++){
            uint32_t free_index = find_free_block();

            set_block_bmp(free_index);
            sync_to_disk_block_bmp();

            superblock_ptr->s_free_blocks_cnt--;
            sync_to_disk_superblock();

            clear_block_index(free_index);
            inode.i_indirect_block_1_ptr = free_index;
            inode.i_fsize += BLOCK_SIZE;

            //TO DO
            inode.i_direct_table[begin_block + i] = free_index;
            sync_to_disk_inode(&inode);
        }
    }
    else if(block_need != 0){
        int i = 1;
        for(; i <= block_need; i++){
            uint32_t free_index = find_free_block();

            set_block_bmp(free_index);
            sync_to_disk_block_bmp();

            superblock_ptr->s_free_blocks_cnt--;
            sync_to_disk_superblock();

            clear_block_index(free_index);
            inode.i_indirect_block_1_ptr = free_index;
            inode.i_fsize += BLOCK_SIZE;

            //TO DO
            inode.i_direct_table[begin_block + i] = free_index;
            sync_to_disk_inode(&inode);
        }
    }

    uint32_t begin_block_index = get_block_index_in_inode(&inode, begin_block);
    uint32_t end_block_index = get_block_index_in_inode(&inode, end_block);

    int i = 0;
    for(i = begin_block_index; i <= end_block_index; i++){
        read_block(i, fwrite_buffer + (i - begin_block_index)*BLOCK_SIZE);
    }

    memcpy(fwrite_buffer + (file_descriptor_table[fd].fd_w_offset % BLOCK_SIZE), buffer, length);

    file_descriptor_table[fd].fd_w_offset += length;

    // int i = 0;
    for(i = begin_block_index; i <= end_block_index; i++){
        write_block(i, fwrite_buffer + (i - begin_block_index)*BLOCK_SIZE);
    }
    return length;
}

int do_fread(int fd, char *buffer, int length)
{
    bzero(fread_buffer, FILE_READ_MAX_LENGTH);

    uint32_t begin_block = file_descriptor_table[fd].fd_r_offset / BLOCK_SIZE;
    uint32_t end_block = (file_descriptor_table[fd].fd_r_offset + length) / BLOCK_SIZE;

    inode_t inode;
    sync_from_disk_inode(file_descriptor_table[fd].fd_inum, &inode);

    uint32_t begin_block_index = get_block_index_in_inode(&inode, begin_block);
    uint32_t end_block_index = get_block_index_in_inode(&inode, end_block);

    int i = 0;
    for(i = begin_block_index; i <= end_block_index; i++){
        read_block(i, fread_buffer + (i - begin_block_index)*BLOCK_SIZE);
    }

    memcpy(buffer, fread_buffer + (file_descriptor_table[fd].fd_r_offset % BLOCK_SIZE), length);

    file_descriptor_table[fd].fd_r_offset += length;

    return length;
}

void do_fclose(int fd)
{
    file_descriptor_t file_descriptor;
    bzero(&file_descriptor, sizeof(file_descriptor_t));
    memcpy((uint8_t *)&file_descriptor_table[fd], (uint8_t *)&file_descriptor, sizeof(file_descriptor_t));
}

int do_touch(char *name, mode_t mode)
{
    bzero(parent_buffer, MAX_PATH_LENGTH);
    bzero(path_buffer, MAX_PATH_LENGTH);
    bzero(name_buffer, MAX_NAME_LENGTH);

    char *p = "./";
    strcpy(path_buffer, p);
    strcpy(path_buffer+2, name);

    // separate_path(path, parent, name);
    separate_path(path_buffer, parent_buffer, name_buffer);

    uint32_t parent_inum = 0, free_inum, free_block_index;
    // parent_inum = parse_path(parent, current_dir_ptr);
    parent_inum = find_file(current_dir_ptr, parent_buffer);
    
    sync_from_disk_block_bmp();
    sync_from_disk_inode_bmp();

    inode_t parent_inode, new_inode;
    sync_from_disk_inode(parent_inum, &parent_inode);

    if(find_dentry(&parent_inode, name_buffer) != -1){
        vt100_move_cursor(1, 45);
        printk("[FS ERROR] ERROR_DUP_DIR_NAME\n");
        return ERROR_DUP_DIR_NAME;
    }

    free_inum = find_free_inode();
    set_inode_bmp(free_inum);
    sync_to_disk_inode_bmp();

    superblock_ptr->s_free_inode_cnt--;
    sync_to_disk_superblock();

    // new_inode.i_fmode = S_IFDIR | mode;
    new_inode.i_fmode = S_IFREG | mode;
    new_inode.i_links_cnt = 1;
    // new_inode.i_fsize = BLOCK_SIZE;
    new_inode.i_fsize = 0;
    new_inode.i_fnum = 0;
    new_inode.i_atime = get_ticks();
    new_inode.i_ctime = get_ticks();
    new_inode.i_mtime = get_ticks();
    bzero(new_inode.i_direct_table, MAX_DIRECT_NUM*sizeof(uint32_t));
    // new_inode.i_direct_table[0] = free_block_index;
    new_inode.i_indirect_block_1_ptr = NULL;
    new_inode.i_indirect_block_2_ptr = NULL;
    new_inode.i_indirect_block_3_ptr = NULL;
    new_inode.i_num = free_inum;
    bzero(new_inode.padding, 10*sizeof(uint32_t));

    sync_to_disk_inode(&new_inode);

    dentry_t parent_dentry;
    parent_dentry.d_inum = free_inum;
    strcpy(parent_dentry.d_name, name_buffer);
    write_dentry(&parent_inode, parent_inode.i_fnum+2, &parent_dentry);

    return 0;
}

void do_cat(char *name)
{
    bzero(cat_buffer, CAT_MAX_LENGTH);

    int fd = do_fopen(name, O_RDWR);

    file_descriptor_table[fd].fd_r_offset = 0;
    //important!!!!

    do_fread(fd, cat_buffer, CAT_LENGTH);
    cat_buffer[CAT_LENGTH] = '\0';

}

//-------------------------------------BONUS----------------------------------------------

int do_find(char *path, char *name)
{
    bzero(path_buffer, MAX_PATH_LENGTH);
    bzero(parent_buffer, MAX_PATH_LENGTH);
    bzero(parent_buffer_1, MAX_PATH_LENGTH);
    bzero(parent_buffer_2, MAX_PATH_LENGTH);
    bzero(name_buffer, MAX_NAME_LENGTH);

    memcpy(path_buffer, path, strlen(path));
    parent_buffer[strlen(path)] = '\0';

    char c = '/';

    inode_t _current_dir;
    inode_t *_current_dir_ptr = &_current_dir;
    memcpy((uint8_t *)_current_dir_ptr, (uint8_t *)current_dir_ptr, INODE_SIZE);

    if(count_char_in_string(c, path_buffer) == 0){
        uint32_t inum;
        if((inum = _find_file(_current_dir_ptr, name)) != -1){
        // if((inum = find_dentry(_current_dir_ptr, name)) != -1){
            inode_t ino;
            sync_from_disk_inode(inum, &ino);
            memcpy((int8_t *)_current_dir_ptr, (int8_t *)&ino, sizeof(inode_t));

            return 1;
        }
        
        // return (_find_file(_current_dir_ptr, name) != -1);
        return 0;
    }
    else if(count_char_in_string(c, path_buffer) == 1){
        separate_path(path_buffer, parent_buffer, name_buffer);
        uint32_t inum_1, inum_2;
        if((inum_1 = _find_file(_current_dir_ptr, parent_buffer)) != -1){
        // if((inum_1 = find_dentry(_current_dir_ptr, parent_buffer)) != -1){

            inode_t ino_1;
            sync_from_disk_inode(inum_1, &ino_1);
            memcpy((int8_t *)_current_dir_ptr, (int8_t *)&ino_1, sizeof(inode_t));

            if((inum_2 = _find_file(_current_dir_ptr, name_buffer)) != -1){
            // if((inum_2 = find_dentry(_current_dir_ptr, name_buffer)) != -1){

                inode_t ino_2;
                sync_from_disk_inode(inum_2, &ino_2);
                memcpy((int8_t *)_current_dir_ptr, (int8_t *)&ino_2, sizeof(inode_t));

                return 1;
            }            
        }
        return 0;
        // return (_find_file(_current_dir_ptr, name) != -1);     
    }
    else if(count_char_in_string(c, path_buffer) == 2){
        separate_path(path_buffer, parent_buffer, name_buffer);
        separate_path(parent_buffer, parent_buffer_1, parent_buffer_2);
        uint32_t inum_1, inum_2, inum_3;

        if((inum_1 = _find_file(_current_dir_ptr, parent_buffer_1)) != -1){

            inode_t ino_1;
            sync_from_disk_inode(inum_1, &ino_1);
            memcpy((int8_t *)_current_dir_ptr, (int8_t *)&ino_1, sizeof(inode_t));

            if((inum_2 = _find_file(_current_dir_ptr, parent_buffer_2)) != -1){

                inode_t ino_2;
                sync_from_disk_inode(inum_2, &ino_2);
                memcpy((int8_t *)_current_dir_ptr, (int8_t *)&ino_2, sizeof(inode_t));

                if((inum_3 = _find_file(_current_dir_ptr, name_buffer)) != -1){

                    inode_t ino_3;
                    sync_from_disk_inode(inum_3, &ino_3);
                    memcpy((int8_t *)_current_dir_ptr, (int8_t *)&ino_3, sizeof(inode_t));

                    return 1;
                }  
            }            
        }

        return 0;
        // return (find_file(_current_dir_ptr, name) != -1);   
    }
}


void do_rename(char *old_name, char *new_name)
{
    bzero(parent_buffer, MAX_PATH_LENGTH);
    bzero(path_buffer, MAX_PATH_LENGTH);
    bzero(name_buffer, MAX_NAME_LENGTH);

    char *p = "./";
    strcpy(path_buffer, p);
    strcpy(path_buffer+2, old_name);

    // separate_path(path, parent, name);
    separate_path(path_buffer, parent_buffer, name_buffer);

    uint32_t parent_inum = 0;
    // parent_inum = parse_path(parent, current_dir_ptr);
    parent_inum = find_file(current_dir_ptr, parent_buffer);

    inode_t parent_inode;
    sync_from_disk_inode(parent_inum, &parent_inode);

    inode_t child_inode;
    uint32_t child_inum = 0;
    child_inum = find_file(&parent_inode, name_buffer);

    sync_from_disk_inode(child_inum, &child_inode);

    uint32_t dnum = find_dentry(&parent_inode, old_name);
    dentry_t den;
    bzero(&den, sizeof(dentry_t));
    den.d_inum = child_inum;
    memcpy(den.d_name, new_name, strlen(new_name));
    write_dentry(&parent_inode, dnum, &den);

    return;
}


void do_link(char *src_path, char *new_path)
{
    bzero(parent_buffer, MAX_PATH_LENGTH);
    bzero(path_buffer, MAX_PATH_LENGTH);
    bzero(name_buffer, MAX_NAME_LENGTH);

    uint32_t src_inum = parse_path(src_path, current_dir_ptr);

    inode_t src_inode;
    sync_from_disk_inode(src_inum, &src_inode);

    if(S_ISDIR(src_inode.i_fmode)){
        vt100_move_cursor(1, 45);
        printk("[FS ERROR] ERROR_LINK_CANNOT_BE_DIR\n");
        return ;        
    }

    strcpy(path_buffer, new_path);

    // separate_path(path, parent, name);
    separate_path(path_buffer, parent_buffer, name_buffer); 

    uint32_t parent_inum = parse_path(parent_buffer, current_dir_ptr);

    inode_t parent_inode;
    sync_from_disk_inode(parent_inum, &parent_inode);

    src_inode.i_links_cnt++;
    sync_to_disk_inode(&src_inode);

    dentry_t parent_den;
    parent_den.d_inum = src_inum;
    strcpy(parent_den.d_name, name_buffer);
    write_dentry(&parent_inode, parent_inode.i_fnum+2, &parent_den);

    return;
}

void do_symlink(char *src_path, char *new_path)
{
    bzero(parent_buffer, MAX_PATH_LENGTH);
    bzero(path_buffer, MAX_PATH_LENGTH);
    bzero(name_buffer, MAX_NAME_LENGTH);
    bzero(data_block_buffer, BLOCK_SIZE);

    char *_p = ".";
    strcpy(path_buffer, _p);
    strcpy(path_buffer+1, src_path);
    separate_path(path_buffer, parent_buffer, name_buffer);

    uint32_t parent_inum = 0, free_inum, free_block_index;
    // // parent_inum = parse_path(parent, current_dir_ptr);
    // parent_inum = find_file(current_dir_ptr, parent_buffer);
    parent_inum = parse_path(new_path, current_dir_ptr);
    
    sync_from_disk_block_bmp();
    sync_from_disk_inode_bmp();

    inode_t parent_inode, new_inode;
    sync_from_disk_inode(parent_inum, &parent_inode);

    free_inum = find_free_inode();
    set_inode_bmp(free_inum);
    sync_to_disk_inode_bmp();

    superblock_ptr->s_free_inode_cnt--;
    sync_to_disk_superblock();

    free_block_index = find_free_block();
    set_block_bmp(free_block_index);
    sync_to_disk_block_bmp();

    superblock_ptr->s_free_blocks_cnt--;
    sync_to_disk_superblock();    

    new_inode.i_fmode = S_IFLNK;
    new_inode.i_links_cnt = 1;
    new_inode.i_fsize = BLOCK_SIZE;
    new_inode.i_fnum = 0;
    new_inode.i_atime = get_ticks();
    new_inode.i_ctime = get_ticks();
    new_inode.i_mtime = get_ticks();
    bzero(new_inode.i_direct_table, MAX_DIRECT_NUM*sizeof(uint32_t));
    new_inode.i_direct_table[0] = free_block_index;
    new_inode.i_indirect_block_1_ptr = NULL;
    new_inode.i_indirect_block_2_ptr = NULL;
    new_inode.i_indirect_block_3_ptr = NULL;
    new_inode.i_num = free_inum;
    bzero(new_inode.padding, 10*sizeof(uint32_t));

    sync_to_disk_inode(&new_inode);

    bzero(dentry_block_buffer, BLOCK_SIZE);
    dentry_t *new_dentry_table = (dentry_t *)dentry_block_buffer;
    new_dentry_table[0].d_inum = free_inum;
    strcpy(new_dentry_table[0].d_name, ".");
    new_dentry_table[1].d_inum = parent_inum;
    strcpy(new_dentry_table[1].d_name, "..");
    sync_to_disk_dentry(free_block_index);

    sync_from_disk_file_data(free_block_index);
    memcpy(data_block_buffer + sizeof(dentry_t)*2, (uint8_t *)src_path, strlen(src_path));
    data_block_buffer[sizeof(dentry_t)*2 + strlen(src_path)] = '\0';
    sync_to_disk_file_data(free_block_index);

    dentry_t parent_dentry;
    parent_dentry.d_inum = free_inum;
    strcpy(parent_dentry.d_name, name_buffer);
    write_dentry(&parent_inode, parent_inode.i_fnum+2, &parent_dentry);

    return;
}




//--------------------------------MY OWN COMMAND---------------------------------------


void do_pwd()
{

}

void do_man(char *command)
{

}

void do_du()   
{

}

void do_df()    
{

}

void do_diff(char *name_1, char *name_2) 
{

}

void do_wc(char *name) 
{

}

void do_rm(char *name)
{

}

void do_mv(char *path_1, char *path_2)
{

}

void do_cp(char *path_1, char *path_2) 
{

}

void do_chmod(char *name) 
{

}

