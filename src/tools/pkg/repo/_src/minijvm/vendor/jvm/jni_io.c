//
// Created by gust on 2017/9/1.
//

#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>   // NULL and possibly memcpy, memset
#include "jvm.h"
#ifdef OPEN_SILLICON
#include "../../opensillicon/profile.h"
#endif
#include "garbage.h"
#include "jvm_util.h"
#include "../utils/miniz_wrapper.h"


#ifdef __cplusplus
extern "C" {
#endif



#include <dirent.h>
#include <unistd.h>

s32 isDir(Utf8String *path) {
    struct stat buf;
    return stat(utf8_cstr(path), &buf) == 0 && S_ISDIR(buf.st_mode);
}

Utf8String *getTmpDir() {
    Utf8String *tmps = utf8_create();
#if __JVM_OS_MINGW__ || __JVM_OS_VS__
    c8 buf[128];
    s32 len = GetTempPath(128, buf);
    utf8_append_data(tmps, buf, len);
#else

#ifndef P_tmpdir
#define P_tmpdir "/tmp"
#endif
    utf8_append_c(tmps, P_tmpdir);
#endif
    return tmps;
}
//=================================  native  ====================================


s32 org_mini_fs_InnerFile_openFile(Runtime *runtime, JClass *clazz) {
#ifdef OPEN_SILLICON
    PROFILE_SCOPE(PROFILE_FILE);
#endif
    Instance *name_arr = localvar_getRefer(runtime->localvar, 0);
    Instance *mode_arr = localvar_getRefer(runtime->localvar, 1);
    if (name_arr) {
        FILE *fd = fopen(name_arr->arr_body, mode_arr->arr_body);
        push_long(runtime->stack, (s64) (intptr_t) fd);
    } else {
        push_long(runtime->stack, 0);
    }

#if _JVM_DEBUG_LOG_LEVEL > 5
    invoke_deepth(runtime);
    jvm_printf("org_mini_fs_InnerFile_openFile  \n");
#endif
    return 0;
}

static s32 open_random_file(Runtime *runtime, JClass *clazz) {
#ifdef OPEN_SILLICON
    PROFILE_SCOPE(PROFILE_FILE);
#endif
    Instance *path = localvar_getRefer(runtime->localvar, 0);
    int writable = localvar_getInt(runtime->localvar, 1);
    FILE *file = NULL;
    if (path) {
        int descriptor = open(path->arr_body, writable ? O_RDWR | O_CREAT : O_RDONLY, 0666);
        if (descriptor >= 0) {
            file = fdopen(descriptor, writable ? "rb+" : "rb");
            if (!file) close(descriptor);
        }
    }
    push_long(runtime->stack, (intptr_t) file);
    return RUNTIME_STATUS_NORMAL;
}

static s32 random_file_position(Runtime *runtime, JClass *clazz) {
#ifdef OPEN_SILLICON
    PROFILE_SCOPE(PROFILE_FILE);
#endif
    FILE *file = (void *) (intptr_t) localvar_getLong(runtime->localvar, 0);
    push_long(runtime->stack, file ? ftell(file) : -1);
    return RUNTIME_STATUS_NORMAL;
}

static s32 random_file_length(Runtime *runtime, JClass *clazz) {
#ifdef OPEN_SILLICON
    PROFILE_SCOPE(PROFILE_FILE);
#endif
    FILE *file = (void *) (intptr_t) localvar_getLong(runtime->localvar, 0);
    long position = file ? ftell(file) : -1;
    long length = -1;
    if (position >= 0 && fseek(file, 0, SEEK_END) == 0) {
        length = ftell(file);
        if (fseek(file, position, SEEK_SET) != 0) length = -1;
    }
    push_long(runtime->stack, length);
    return RUNTIME_STATUS_NORMAL;
}

s32 org_mini_fs_InnerFile_closeFile(Runtime *runtime, JClass *clazz) {
#ifdef OPEN_SILLICON
    PROFILE_SCOPE(PROFILE_FILE);
#endif
    Long2Double l2d;
    l2d.l = localvar_getLong(runtime->localvar, 0);
    FILE *fd = (FILE *) (intptr_t) l2d.l;
    s32 ret = -1;
    if (fd) {
        ret = fclose(fd);
    }
    push_int(runtime->stack, ret);
#if _JVM_DEBUG_LOG_LEVEL > 5
    invoke_deepth(runtime);
    jvm_printf("org_mini_fs_InnerFile_closeFile  \n");
#endif
    return 0;
}

s32 org_mini_fs_InnerFile_read0(Runtime *runtime, JClass *clazz) {
#ifdef OPEN_SILLICON
    PROFILE_SCOPE(PROFILE_FILE);
#endif
    Long2Double l2d;
    l2d.l = localvar_getLong(runtime->localvar, 0);
    FILE *fd = (FILE *) (intptr_t) l2d.l;
    s32 ret = -1;
    if (fd) {
        ret = fgetc(fd);
        if (ret == EOF) {
            push_int(runtime->stack, -1);
        } else {
            push_int(runtime->stack, ret);
        }
    } else {
        push_int(runtime->stack, ret);
    }

#if _JVM_DEBUG_LOG_LEVEL > 5
    invoke_deepth(runtime);
    jvm_printf("org_mini_fs_InnerFile_read0  \n");
#endif
    return 0;
}

s32 org_mini_fs_InnerFile_write0(Runtime *runtime, JClass *clazz) {
#ifdef OPEN_SILLICON
    PROFILE_SCOPE(PROFILE_FILE);
#endif
    Long2Double l2d;
    l2d.l = localvar_getLong(runtime->localvar, 0);
    FILE *fd = (FILE *) (intptr_t) l2d.l;
    u8 byte = (u8) localvar_getInt(runtime->localvar, 2);
    s32 ret = -1;
    if (fd) {
        ret = fputc(byte, fd);
        if (ret == EOF) {
            push_int(runtime->stack, -1);
        } else {
            push_int(runtime->stack, byte);
        }
    } else {
        push_int(runtime->stack, ret);
    }

#if _JVM_DEBUG_LOG_LEVEL > 5
    invoke_deepth(runtime);
    jvm_printf("org_mini_fs_InnerFile_write0  \n");
#endif
    return 0;
}

/* Avoid one VM transition and one descriptor write per byte of a region
 * header. Keep each Java write visible immediately; no deferred write cache. */
static s32 org_mini_fs_InnerRandomAccessFile_writeNumber0(Runtime *runtime, JClass *clazz) {
#ifdef OPEN_SILLICON
    PROFILE_SCOPE(PROFILE_FILE);
#endif
    FILE *fd=(FILE *)(intptr_t)localvar_getLong(runtime->localvar,0);
    u64 value=(u64)localvar_getLong(runtime->localvar,2);
    s32 bytes=localvar_getInt(runtime->localvar,4);
    unsigned char buffer[8];
    s32 result=-1;
    if(fd && (bytes==2 || bytes==4 || bytes==8)) {
        for(s32 i=0;i<bytes;i++)buffer[i]=(unsigned char)(value >> ((bytes-i-1)*8));
        size_t done=0;
        while(done<(size_t)bytes) {
            size_t n=fwrite(buffer+done,1,(size_t)bytes-done,fd);
            if(!n)break;
            done+=n;
        }
        if(done==(size_t)bytes)result=bytes;
    }
    push_int(runtime->stack,result);
    return RUNTIME_STATUS_NORMAL;
}

s32 org_mini_fs_InnerFile_readbuf(Runtime *runtime, JClass *clazz) {
#ifdef OPEN_SILLICON
    PROFILE_SCOPE(PROFILE_FILE);
#endif
    s32 pos = 0;
    Long2Double l2d;
    l2d.l = localvar_getLong(runtime->localvar, pos);
    pos += 2;
    FILE *fd = (FILE *) (intptr_t) l2d.l;
    Instance *bytes_arr = localvar_getRefer(runtime->localvar, pos++);
    s32 offset = localvar_getInt(runtime->localvar, pos++);
    s32 len = localvar_getInt(runtime->localvar, pos++);
    s32 ret = -1;
    if (fd && bytes_arr) {
        ret = (s32) fread(bytes_arr->arr_body + offset, 1, len, fd);
    }
    if (ret == 0) {
        ret = -1;
    }
    push_int(runtime->stack, ret);

#if _JVM_DEBUG_LOG_LEVEL > 5
    invoke_deepth(runtime);
    jvm_printf("org_mini_fs_InnerFile_readbuf  \n");
#endif
    return 0;
}

s32 org_mini_fs_InnerFile_writebuf(Runtime *runtime, JClass *clazz) {
#ifdef OPEN_SILLICON
    PROFILE_SCOPE(PROFILE_FILE);
#endif
    s32 pos = 0;
    Long2Double l2d;
    l2d.l = localvar_getLong(runtime->localvar, pos);
    pos += 2;
    FILE *fd = (FILE *) (intptr_t) l2d.l;
    Instance *bytes_arr = localvar_getRefer(runtime->localvar, pos++);
    s32 offset = localvar_getInt(runtime->localvar, pos++);
    s32 len = localvar_getInt(runtime->localvar, pos++);
    s32 ret = -1;
    if (fd && bytes_arr) {
        ret = (s32) fwrite(bytes_arr->arr_body + offset, 1, len, fd);
		if (ret == 0 && len != 0) ret = -1;
        push_int(runtime->stack, ret);
    } else {
        push_int(runtime->stack, ret);
    }

#if _JVM_DEBUG_LOG_LEVEL > 5
    invoke_deepth(runtime);
    jvm_printf("org_mini_fs_InnerFile_writebuf  \n");
#endif
    return 0;
}

s32 org_mini_fs_InnerFile_seek0(Runtime *runtime, JClass *clazz) {
#ifdef OPEN_SILLICON
    PROFILE_SCOPE(PROFILE_FILE);
#endif
    s32 pos = 0;
    Long2Double l2d;
    l2d.l = localvar_getLong(runtime->localvar, pos);
    pos += 2;
    FILE *fd = (FILE *) (intptr_t) l2d.l;
    l2d.l = localvar_getLong(runtime->localvar, pos);
    pos += 2;
    s64 filepos = l2d.l;
    s32 ret = -1;
    if (fd) {
        ret = fseek(fd, (long) filepos, SEEK_SET);
    }
    push_int(runtime->stack, ret);
#if _JVM_DEBUG_LOG_LEVEL > 5
    invoke_deepth(runtime);
    jvm_printf("org_mini_fs_InnerFile_seek0  \n");
#endif
    return 0;
}

s32 org_mini_fs_InnerFile_available0(Runtime *runtime, JClass *clazz) {
#ifdef OPEN_SILLICON
    PROFILE_SCOPE(PROFILE_FILE);
#endif
    s32 pos = 0;
    Long2Double l2d;
    l2d.l = localvar_getLong(runtime->localvar, pos);
    pos += 2;
    FILE *fd = (FILE *) (intptr_t) l2d.l;

    s32 cur = 0, end = 0;
    if (fd) {
        cur = ftell(fd);
        fseek(fd, (long) 0, SEEK_END);
        end = ftell(fd);
        fseek(fd, (long) cur, SEEK_SET);
    }
    push_int(runtime->stack, end - cur);
#if _JVM_DEBUG_LOG_LEVEL > 5
    invoke_deepth(runtime);
    jvm_printf("org_mini_fs_InnerFile_available0  \n");
#endif
    return 0;
}

s32 org_mini_fs_InnerFile_setLength0(Runtime *runtime, JClass *clazz) {
#ifdef OPEN_SILLICON
    PROFILE_SCOPE(PROFILE_FILE);
#endif
    FILE *fd = (FILE *) (intptr_t) localvar_getLong(runtime->localvar, 0);
    s64 length = localvar_getLong(runtime->localvar, 2);
    int result = -1;

    if (fd && length >= 0) {
        long position = ftell(fd);
        if (position >= 0 && fseek(fd, 0, SEEK_END) == 0) {
            long size = ftell(fd);
            /* libos can extend a file by writing, but cannot shrink it. */
            if (size >= 0 && length >= size &&
                (length == size ||
                 (fseek(fd, length - 1, SEEK_SET) == 0 && fputc(0, fd) != EOF)))
                result = 0;
            if (fseek(fd, position, SEEK_SET))
                result = -1;
        }
    }
    push_int(runtime->stack, result);
    return 0;
}

s32 org_mini_fs_InnerFile_flush0(Runtime *runtime, JClass *clazz) {
#ifdef OPEN_SILLICON
    PROFILE_SCOPE(PROFILE_FILE);
#endif
    Long2Double l2d;
    l2d.l = localvar_getLong(runtime->localvar, 0);
    FILE *fd = (FILE *) (intptr_t) l2d.l;
    s32 ret = -1;
    if (fd) {
        ret = fflush(fd);
    }
    push_int(runtime->stack, ret);
#if _JVM_DEBUG_LOG_LEVEL > 5
    invoke_deepth(runtime);
    jvm_printf("org_mini_fs_InnerFile_flush0  \n");
#endif
    return 0;
}

s32 org_mini_fs_InnerFile_loadFS(Runtime *runtime, JClass *clazz) {
#ifdef OPEN_SILLICON
    PROFILE_SCOPE(PROFILE_FILE);
#endif
    Instance *name_arr = localvar_getRefer(runtime->localvar, 0);
    Instance *fd = localvar_getRefer(runtime->localvar, 1);
    s32 ret = -1;
    if (name_arr) {
        Utf8String *filepath = utf8_create_part_c(name_arr->arr_body, 0, name_arr->arr_length);
        struct stat buf;
        ret = stat(utf8_cstr(filepath), &buf);
        s32 a = S_ISDIR(buf.st_mode);
        if (ret == 0) {
            c8 *className = "org/mini/fs/InnerFileStat";
            c8 *ptr;
            ptr = getFieldPtr_byName_c(fd, className, "st_dev", "I", runtime);
            setFieldInt(ptr, buf.st_dev);
            ptr = getFieldPtr_byName_c(fd, className, "st_ino", "S", runtime);
            setFieldShort(ptr, buf.st_ino);
            ptr = getFieldPtr_byName_c(fd, className, "st_mode", "S", runtime);
            setFieldShort(ptr, buf.st_mode);
            ptr = getFieldPtr_byName_c(fd, className, "st_nlink", "S", runtime);
            setFieldShort(ptr, buf.st_nlink);
            ptr = getFieldPtr_byName_c(fd, className, "st_uid", "S", runtime);
            setFieldShort(ptr, buf.st_uid);
            ptr = getFieldPtr_byName_c(fd, className, "st_gid", "S", runtime);
            setFieldShort(ptr, buf.st_gid);
            ptr = getFieldPtr_byName_c(fd, className, "st_rdev", "S", runtime);
            setFieldShort(ptr, buf.st_rdev);
            ptr = getFieldPtr_byName_c(fd, className, "st_size", "J", runtime);
            setFieldLong(ptr, buf.st_size);
            ptr = getFieldPtr_byName_c(fd, className, "st_atime", "J", runtime);
            setFieldLong(ptr, buf.st_atime);
            ptr = getFieldPtr_byName_c(fd, className, "st_mtime", "J", runtime);
            setFieldLong(ptr, buf.st_mtime);
            ptr = getFieldPtr_byName_c(fd, className, "st_ctime", "J", runtime);
            setFieldLong(ptr, buf.st_ctime);
            ptr = getFieldPtr_byName_c(fd, className, "exists", "Z", runtime);
            setFieldByte(ptr, 1);
        }
        utf8_destory(filepath);
    }
    push_int(runtime->stack, ret);
#if _JVM_DEBUG_LOG_LEVEL > 5
    invoke_deepth(runtime);
    jvm_printf("org_mini_fs_InnerFile_loadFD  \n");
#endif
    return 0;
}

s32 org_mini_fs_InnerFile_listDir(Runtime *runtime, JClass *clazz) {
#ifdef OPEN_SILLICON
    PROFILE_SCOPE(PROFILE_FILE);
#endif
    Instance *name_arr = localvar_getRefer(runtime->localvar, 0);
    if (name_arr) {
        Utf8String *filepath = utf8_create_part_c(name_arr->arr_body, 0, name_arr->arr_length);

        ArrayList *files = arraylist_create(0);
        DIR *dirp;
        struct dirent *dp;
        dirp = opendir(utf8_cstr(filepath)); //打开目录指针
        if (dirp) {
            while ((dp = readdir(dirp)) != NULL) { //通过目录指针读目录
                if (strcmp(dp->d_name, ".") == 0) {
                    continue;
                }
                if (strcmp(dp->d_name, "..") == 0) {
                    continue;
                }
                Utf8String *ustr = utf8_create_c(dp->d_name);
                Instance *jstr = jstring_create(ustr, runtime);
                instance_hold_to_thread(jstr, runtime);
                utf8_destory(ustr);
                arraylist_push_back(files, jstr);
            }
            (void) closedir(dirp); //关闭目录

            s32 i;
            Utf8String *ustr = utf8_create_c(STR_CLASS_JAVA_LANG_STRING);
            Instance *jarr = jarray_create_by_type_name(runtime, files->length, ustr);
            utf8_destory(ustr);
            for (i = 0; i < files->length; i++) {
                __refer ref = arraylist_get_value(files, i);
                instance_release_from_thread(ref, runtime);
                jarray_set_field(jarr, i, (intptr_t) ref);
            }
            push_ref(runtime->stack, jarr);
        } else {
            push_ref(runtime->stack, NULL);
        }
        arraylist_destory(files);
        utf8_destory(filepath);
    } else {
        push_ref(runtime->stack, NULL);
    }
#if _JVM_DEBUG_LOG_LEVEL > 5
    invoke_deepth(runtime);
    jvm_printf("org_mini_fs_InnerFile_listDir  \n");
#endif
    return 0;
}

s32 org_mini_fs_InnerFile_getcwd(Runtime *runtime, JClass *clazz) {
#ifdef OPEN_SILLICON
    PROFILE_SCOPE(PROFILE_FILE);
#endif
    Instance *path_arr = localvar_getRefer(runtime->localvar, 0);
    if (path_arr) {
        __refer ret = getcwd(path_arr->arr_body, path_arr->arr_length);
        push_int(runtime->stack, ret == path_arr->arr_body ? 0 : -1);
    } else {
        push_int(runtime->stack, -1);
    }
#if _JVM_DEBUG_LOG_LEVEL > 5
    invoke_deepth(runtime);
    jvm_printf("org_mini_fs_InnerFile_getcwd  \n");
#endif
    return 0;
}

s32 org_mini_fs_InnerFile_chmod(Runtime *runtime, JClass *clazz) {
#ifdef OPEN_SILLICON
    PROFILE_SCOPE(PROFILE_FILE);
#endif
    Instance *path_arr = localvar_getRefer(runtime->localvar, 0);
    s32 mode = localvar_getInt(runtime->localvar, 1);
    if (path_arr) {
        s32 ret = chmod(path_arr->arr_body, mode);
        push_int(runtime->stack, ret);
    } else {
        push_int(runtime->stack, -1);
    }
#if _JVM_DEBUG_LOG_LEVEL > 5
    invoke_deepth(runtime);
    jvm_printf("org_mini_fs_InnerFile_fullpath  \n");
#endif
    return 0;
}

s32 org_mini_fs_InnerFile_rename0(Runtime *runtime, JClass *clazz) {
#ifdef OPEN_SILLICON
    PROFILE_SCOPE(PROFILE_FILE);
#endif
    Instance *old_arr = localvar_getRefer(runtime->localvar, 0);
    Instance *new_arr = localvar_getRefer(runtime->localvar, 1);
    if (old_arr && new_arr) {
#ifdef OPEN_SILLICON
        extern int minijvm_rename(const char *, const char *);
        s32 ret = minijvm_rename(old_arr->arr_body, new_arr->arr_body);
#else
        s32 ret = rename(old_arr->arr_body, new_arr->arr_body);
#endif
        push_int(runtime->stack, ret);
    } else {
        push_int(runtime->stack, -1);
    }
#if _JVM_DEBUG_LOG_LEVEL > 5
    invoke_deepth(runtime);
    jvm_printf("org_mini_fs_InnerFile_rename0  \n");
#endif
    return 0;
}

s32 org_mini_fs_InnerFile_getTmpDir(Runtime *runtime, JClass *clazz) {
#ifdef OPEN_SILLICON
    PROFILE_SCOPE(PROFILE_FILE);
#endif
    Utf8String *tdir = getTmpDir();
    if (tdir) {
        Instance *jstr = jstring_create(tdir, runtime);
        utf8_destory(tdir);
        push_ref(runtime->stack, jstr);
    } else {
        push_ref(runtime->stack, NULL);
    }
#if _JVM_DEBUG_LOG_LEVEL > 5
    invoke_deepth(runtime);
    jvm_printf("org_mini_fs_InnerFile_getTmpDir  \n");
#endif
    return 0;
}

s32 org_mini_fs_InnerFile_mkdir0(Runtime *runtime, JClass *clazz) {
#ifdef OPEN_SILLICON
    PROFILE_SCOPE(PROFILE_FILE);
#endif
    Instance *path_arr = localvar_getRefer(runtime->localvar, 0);
    s32 ret = -1;
    if (path_arr) {
#if __JVM_OS_MINGW__ || __JVM_OS_VS__
        ret = mkdir(path_arr->arr_body);
#else
        ret = mkdir(path_arr->arr_body, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
#endif
        push_int(runtime->stack, ret);
    } else {
        push_int(runtime->stack, ret);
    }
#if _JVM_DEBUG_LOG_LEVEL > 5
    invoke_deepth(runtime);
    jvm_printf("org_mini_fs_InnerFile_mkdir  \n");
#endif
    return 0;
}

s32 org_mini_fs_InnerFile_getOS(Runtime *runtime, JClass *clazz) {
#ifdef OPEN_SILLICON
    PROFILE_SCOPE(PROFILE_FILE);
#endif
#if __JVM_OS_VS__ || __JVM_OS_MINGW__ || __JVM_OS_CYGWIN__
    push_int(runtime->stack, 1);
#else
    push_int(runtime->stack, 0);
#endif
#if _JVM_DEBUG_LOG_LEVEL > 5
    invoke_deepth(runtime);
    jvm_printf("org_mini_fs_InnerFile_getOS  \n");
#endif
    return 0;
}

s32 org_mini_fs_InnerFile_delete0(Runtime *runtime, JClass *clazz) {
#ifdef OPEN_SILLICON
    PROFILE_SCOPE(PROFILE_FILE);
#endif
    Instance *path_arr = localvar_getRefer(runtime->localvar, 0);
    s32 ret = -1;
    if (path_arr) {
        struct stat buf;
        stat(path_arr->arr_body, &buf);
        s32 a = S_ISDIR(buf.st_mode);
        if (a) {
            ret = rmdir(path_arr->arr_body);
        } else {
            ret = remove(path_arr->arr_body);
        }
        push_int(runtime->stack, ret);
    } else {
        push_int(runtime->stack, ret);
    }
#if _JVM_DEBUG_LOG_LEVEL > 5
    invoke_deepth(runtime);
    jvm_printf("org_mini_fs_InnerFile_delete0  \n");
#endif
    return 0;
}

static s32 zip_entry_info0(Runtime *runtime, JClass *clazz) {
    Instance *path = localvar_getRefer(runtime->localvar, 0);
    Instance *name = localvar_getRefer(runtime->localvar, 1);
    Instance *info = NULL;
    u64 size; s32 directory;
    if (path && name && !zip_entry_info(path->arr_body, name->arr_body, &size, &directory)) {
        info = jarray_create_by_type_index(runtime, 2, DATATYPE_LONG);
        if (info) { ((s64 *)info->arr_body)[0] = size; ((s64 *)info->arr_body)[1] = directory; }
    }
    push_ref(runtime->stack, info);
    return RUNTIME_STATUS_NORMAL;
}

s32 org_mini_zip_ZipFile_getEntry0(Runtime *runtime, JClass *clazz) {
    Instance *zip_path_arr = localvar_getRefer(runtime->localvar, 0);
    Instance *name_arr = localvar_getRefer(runtime->localvar, 1);
    s32 ret = -1;
    if (zip_path_arr && name_arr) {
        ByteBuf *buf = bytebuf_create(0);
        int loaded = zip_loadfile(zip_path_arr->arr_body, name_arr->arr_body, buf);
        if (!loaded) {
            Instance *arr = jarray_create_by_type_index(runtime, buf->wp, DATATYPE_BYTE);
            memmove(arr->arr_body, buf->buf, buf->wp);
            push_ref(runtime->stack, arr);
            ret = 0;
        }
        bytebuf_destory(buf);
    }
    if (ret) {
        push_ref(runtime->stack, NULL);
    }
#if _JVM_DEBUG_LOG_LEVEL > 5
    invoke_deepth(runtime);
    jvm_printf("org_mini_zip_ZipFile_getEntry0  \n");
#endif
    return 0;
}

s32 org_mini_zip_ZipFile_putEntry0(Runtime *runtime, JClass *clazz) {
    Instance *zip_path_arr = localvar_getRefer(runtime->localvar, 0);
    Instance *name_arr = localvar_getRefer(runtime->localvar, 1);
    Instance *content_arr = localvar_getRefer(runtime->localvar, 2);
    s32 ret = -1;
    if (zip_path_arr && name_arr && content_arr) {
        zip_savefile_mem(zip_path_arr->arr_body, name_arr->arr_body, content_arr->arr_body, content_arr->arr_length);
        ret = 0;
    }
    push_int(runtime->stack, ret);
#if _JVM_DEBUG_LOG_LEVEL > 5
    invoke_deepth(runtime);
    jvm_printf("org_mini_zip_ZipFile_putEntry0  \n");
#endif
    return 0;
}

s32 org_mini_zip_ZipFile_fileCount0(Runtime *runtime, JClass *clazz) {
    Instance *zip_path_arr = localvar_getRefer(runtime->localvar, 0);

    s32 ret = 0;
    if (zip_path_arr) {
        ret = zip_filecount(zip_path_arr->arr_body);
    }
    push_int(runtime->stack, ret);
#if _JVM_DEBUG_LOG_LEVEL > 5
    invoke_deepth(runtime);
    jvm_printf("org_mini_zip_ZipFile_fileCount0  \n");
#endif
    return 0;
}

s32 org_mini_zip_ZipFile_listFiles0(Runtime *runtime, JClass *clazz) {
    Instance *zip_path_arr = localvar_getRefer(runtime->localvar, 0);
    s32 ret = -1;
    if (zip_path_arr) {
        ArrayList *list = zip_get_filenames(zip_path_arr->arr_body);
        if (list) {
            Utf8String *clustr = utf8_create_c(STR_CLASS_JAVA_LANG_STRING);
            Instance *jarr = jarray_create_by_type_name(runtime, list->length, clustr);
            utf8_destory(clustr);
            instance_hold_to_thread(jarr, runtime);
            s32 i;
            for (i = 0; i < list->length; i++) {
                Utf8String *ustr = arraylist_get_value_unsafe(list, i);
                Instance *jstr = jstring_create(ustr, runtime);
                jarray_set_field(jarr, i, (s64) (intptr_t) jstr);
            }
            zip_destory_filenames_list(list);
            instance_release_from_thread(jarr, runtime);
            push_ref(runtime->stack, jarr);
            ret = 0;
        }
    }
    if (ret == -1) {
        push_ref(runtime->stack, NULL);
    }
#if _JVM_DEBUG_LOG_LEVEL > 5
    invoke_deepth(runtime);
    jvm_printf("org_mini_zip_ZipFile_listFiles0  \n");
#endif
    return 0;
}

s32 org_mini_zip_ZipFile_isDirectory0(Runtime *runtime, JClass *clazz) {
    Instance *zip_path_arr = localvar_getRefer(runtime->localvar, 0);
    s32 index = localvar_getInt(runtime->localvar, 1);
    s32 ret = -1;
    if (zip_path_arr) {
        ret = zip_is_directory(zip_path_arr->arr_body, index);
    }

    push_int(runtime->stack, ret);

#if _JVM_DEBUG_LOG_LEVEL > 5
    invoke_deepth(runtime);
    jvm_printf("org_mini_zip_ZipFile_isDirectory0  \n");
#endif
    return 0;
}

s32 org_mini_zip_ZipFile_extract0(Runtime *runtime, JClass *clazz) {
    Instance *zip_data = localvar_getRefer(runtime->localvar, 0);
    s32 ret = 0;
    ByteBuf *data = bytebuf_create(0);
    if (zip_data) {
        ret = zip_extract(zip_data->arr_body, zip_data->arr_length, data);
    }
    if (ret == -1) {
        push_ref(runtime->stack, NULL);
    } else {
        Instance *byte_arr = jarray_create_by_type_index(runtime, data->wp, DATATYPE_BYTE);
        bytebuf_read_batch(data, byte_arr->arr_body, data->wp);
        push_ref(runtime->stack, byte_arr);
    }
    bytebuf_destory(data);
#if _JVM_DEBUG_LOG_LEVEL > 5
    invoke_deepth(runtime);
    jvm_printf("org_mini_zip_ZipFile_extract0  \n");
#endif
    return 0;
}

s32 org_mini_zip_ZipFile_compress0(Runtime *runtime, JClass *clazz) {
    Instance *data = localvar_getRefer(runtime->localvar, 0);
    s32 ret = 0;
    ByteBuf *zip_data = bytebuf_create(0);
    if (data) {
        ret = zip_compress(data->arr_body, data->arr_length, zip_data);
    }
    if (ret == -1) {
        push_ref(runtime->stack, NULL);
    } else {
        Instance *byte_arr = jarray_create_by_type_index(runtime, zip_data->wp, DATATYPE_BYTE);
        bytebuf_read_batch(zip_data, byte_arr->arr_body, zip_data->wp);
        push_ref(runtime->stack, byte_arr);
    }
    bytebuf_destory(zip_data);
#if _JVM_DEBUG_LOG_LEVEL > 5
    invoke_deepth(runtime);
    jvm_printf("org_mini_zip_ZipFile_compress0  \n");
#endif
    return 0;
}

s32 org_mini_crypt_XorCrypt_encrypt(Runtime *runtime, JClass *clazz) {
    s32 pos = 0;
    Instance *data = localvar_getRefer(runtime->localvar, pos++);
    Instance *key = localvar_getRefer(runtime->localvar, pos++);
    if (data && key) {
        Instance *r = jarray_create_by_type_index(runtime, data->arr_length, DATATYPE_BYTE);
        s32 i, j, imax, jmax;
        for (i = 0, imax = data->arr_length; i < imax; i++) {
            u32 v = jarray_get_field(data, i) & 0xff;
            for (j = 0, jmax = key->arr_length; j < jmax; j++) {
                u32 k = jarray_get_field(key, j) & 0xff;

                u32 bitshift = k % 8;

                u32 v1 = (v << bitshift);
                u32 v2 = (v >> (8 - bitshift));
                v = (v1 | v2);

                v = (v ^ k) & 0xff;
            }
            jarray_set_field(r, i, v & 0xff);
        }
        push_ref(runtime->stack, r);
    } else {
        push_ref(runtime->stack, NULL);
    }
    return 0;
}

s32 org_mini_crypt_XorCrypt_decrypt(Runtime *runtime, JClass *clazz) {
    s32 pos = 0;
    Instance *data = localvar_getRefer(runtime->localvar, pos++);
    Instance *key = localvar_getRefer(runtime->localvar, pos++);
    if (data && key) {
        Instance *r = jarray_create_by_type_index(runtime, data->arr_length, DATATYPE_BYTE);
        s32 i, j, imax;
        for (i = 0, imax = data->arr_length; i < imax; i++) {
            u32 v = jarray_get_field(data, i) & 0xff;
            for (j = key->arr_length - 1; j >= 0; j--) {
                u32 k = jarray_get_field(key, j) & 0xff;
                v = (v ^ k) & 0xff;

                u32 bitshift = k % 8;

                u32 v1 = (v >> bitshift);
                u32 v2 = (v << (8 - bitshift));
                v = (v1 | v2);

            }
            jarray_set_field(r, i, v & 0xff);
        }
        push_ref(runtime->stack, r);
    } else {
        push_ref(runtime->stack, NULL);
    }
    return 0;
}

static java_native_method METHODS_IO_TABLE[] = {
        {"org/mini/fs/InnerRandomAccessFile", "position0", "(J)J", random_file_position},
        {"org/mini/fs/InnerRandomAccessFile", "length0", "(J)J", random_file_length},
        {"org/mini/fs/InnerRandomAccessFile", "openRandomFile", "([BZ)J", open_random_file},
        {"org/mini/fs/InnerFile",     "openFile",             "([B[B)J",                          org_mini_fs_InnerFile_openFile},
        {"org/mini/fs/InnerFile",     "closeFile",            "(J)I",                             org_mini_fs_InnerFile_closeFile},
        {"org/mini/fs/InnerFile",     "read0",                "(J)I",                             org_mini_fs_InnerFile_read0},
        {"org/mini/fs/InnerRandomAccessFile", "writeNumber0", "(JJI)I", org_mini_fs_InnerRandomAccessFile_writeNumber0},
        {"org/mini/fs/InnerFile",     "write0",               "(JI)I",                            org_mini_fs_InnerFile_write0},
        {"org/mini/fs/InnerFile",     "readbuf",              "(J[BII)I",                         org_mini_fs_InnerFile_readbuf},
        {"org/mini/fs/InnerFile",     "writebuf",             "(J[BII)I",                         org_mini_fs_InnerFile_writebuf},
        {"org/mini/fs/InnerFile",     "seek0",                "(JJ)I",                            org_mini_fs_InnerFile_seek0},
        {"org/mini/fs/InnerFile",     "available0",           "(J)I",                             org_mini_fs_InnerFile_available0},
        {"org/mini/fs/InnerFile",     "setLength0",           "(JJ)I",                            org_mini_fs_InnerFile_setLength0},
        {"org/mini/fs/InnerFile",     "flush0",               "(J)I",                             org_mini_fs_InnerFile_flush0},
        {"org/mini/fs/InnerFile",     "loadFS",               "([BLorg/mini/fs/InnerFileStat;)I", org_mini_fs_InnerFile_loadFS},
        {"org/mini/fs/InnerFile",     "listDir",              "([B)[Ljava/lang/String;",          org_mini_fs_InnerFile_listDir},
        {"org/mini/fs/InnerFile",     "getcwd",               "([B)I",                            org_mini_fs_InnerFile_getcwd},
        {"org/mini/fs/InnerFile",     "chmod",                "([BI)I",                           org_mini_fs_InnerFile_chmod},
        {"org/mini/fs/InnerFile",     "mkdir0",               "([B)I",                            org_mini_fs_InnerFile_mkdir0},
        {"org/mini/fs/InnerFile",     "getOS",                "()I",                              org_mini_fs_InnerFile_getOS},
        {"org/mini/fs/InnerFile",     "delete0",              "([B)I",                            org_mini_fs_InnerFile_delete0},
        {"org/mini/fs/InnerFile",     "rename0",              "([B[B)I",                          org_mini_fs_InnerFile_rename0},
        {"org/mini/fs/InnerFile",     "getTmpDir",            "()Ljava/lang/String;",             org_mini_fs_InnerFile_getTmpDir},
        {"org/mini/zip/Zip", "entryInfo0", "([B[B)[J", zip_entry_info0},
        {"org/mini/zip/Zip",          "getEntry0",            "([B[B)[B",                         org_mini_zip_ZipFile_getEntry0},
        {"org/mini/zip/Zip",          "putEntry0",            "([B[B[B)I",                        org_mini_zip_ZipFile_putEntry0},
        {"org/mini/zip/Zip",          "fileCount0",           "([B)I",                            org_mini_zip_ZipFile_fileCount0},
        {"org/mini/zip/Zip",          "listFiles0",           "([B)[Ljava/lang/String;",          org_mini_zip_ZipFile_listFiles0},
        {"org/mini/zip/Zip",          "isDirectory0",         "([BI)I",                           org_mini_zip_ZipFile_isDirectory0},
        {"org/mini/zip/Zip",          "extract0",             "([B)[B",                           org_mini_zip_ZipFile_extract0},
        {"org/mini/zip/Zip",          "compress0",            "([B)[B",                           org_mini_zip_ZipFile_compress0},
        {"org/mini/crypt/XorCrypt",   "encrypt",              "([B[B)[B",                         org_mini_crypt_XorCrypt_encrypt},
        {"org/mini/crypt/XorCrypt",   "decrypt",              "([B[B)[B",                         org_mini_crypt_XorCrypt_decrypt},

};


void reg_net_native_lib(MiniJVM *jvm) {
    native_reg_lib(jvm, &(METHODS_IO_TABLE[0]), sizeof(METHODS_IO_TABLE) / sizeof(java_native_method));
}


#ifdef __cplusplus
}
#endif
