/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */
package org.mini.fs;

import java.io.File;

/**
 *
 * @author gust
 */
public class FileSystemPosix extends FileSystemImpl {

    @Override
    public String normalize(String path) {
        path = path.replace('\\', getSeparator());
        path = super.normalize(path);
        return path;
    }

    @Override
    public boolean isAbsolute(String path) {
        return path.length() > 0 && path.charAt(0) == getSeparator();
    }

    public String getRegexParentTag() {
        return "/[^/]+/\\.\\.";
    }

    @Override
    public int prefixLength(String path) {
        return isAbsolute(path) ? 1 : 0;
    }
}
