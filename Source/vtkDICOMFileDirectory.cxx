/*=========================================================================

  Program: DICOM for VTK

  Copyright (c) 2012-2015 David Gobbi
  All rights reserved.
  See Copyright.txt or http://dgobbi.github.io/bsd3.txt for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/

#include "vtkDICOMFileDirectory.h"
#include "vtkDICOMFilePath.h"
#include "vtkDICOMFile.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#endif

#include <string>
#include <vector>

// for PGI compiler, use dirent64 if readdir is readdir64
#if defined(__PGI) && defined(__GLIBC__)
# define redef_dirent_readdir dirent
# define redef_dirent_readdir64 dirent64
# define redef_dirent redef_dirent_lookup(readdir)
# define redef_dirent_lookup(x) redef_dirent_lookup2(x)
# define redef_dirent_lookup2(x) redef_dirent_##x
#else
# define redef_dirent dirent
#endif

//----------------------------------------------------------------------------
struct vtkDICOMFileDirectory::Entry
{
  std::string Name;
  unsigned short Flags;
  unsigned short Mask;
};

//----------------------------------------------------------------------------
vtkDICOMFileDirectory::vtkDICOMFileDirectory(const char *dirname)
  : Name(dirname), Error(0), NumberOfFiles(0), Entries(0)
{
#ifdef _WIN32
  vtkDICOMFilePath path(dirname);
  path.PushBack("*");
  const wchar_t *widename = path.Wide();
  if (widename == 0)
    {
    this->Error = UnknownError;
    }
  else
    {
    WIN32_FIND_DATAW fileData;
    HANDLE h = FindFirstFileW(widename, &fileData);
    DWORD code = 0;
    if (h == INVALID_HANDLE_VALUE)
      {
      code = GetLastError();
      if (code == ERROR_FILE_NOT_FOUND)
        {
        code = ERROR_NO_MORE_FILES;
        }
      }
    else
      {
      // each utf-16 wchar converts to three or fewer utf-8 bytes
      int n = MAX_PATH*3;
      char name[MAX_PATH*3];
      do
        {
        WideCharToMultiByte(
          CP_UTF8, 0, fileData.cFileName, -1, name, n, 0, 0);
        unsigned int flags = 0;
        if ((fileData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 &&
            fileData.dwReserved0 == IO_REPARSE_TAG_SYMLINK)
          {
          flags |= TypeSymlink;
          }
        if ((fileData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0)
          {
          flags |= TypeHidden;
          }
        if ((fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
          {
          flags |= TypeDirectory;
          }
        this->AddEntry(name, flags, (TypeSymlink | TypeDirectory));
        }
      while (FindNextFileW(h, &fileData));
      code = GetLastError();
      }
    if (code == ERROR_ACCESS_DENIED)
      {
      this->Error = AccessDenied;
      }
    else if (code == ERROR_FILE_NOT_FOUND ||
             code == ERROR_PATH_NOT_FOUND)
      {
      this->Error = FileNotFound;
      }
    else if (code != ERROR_NO_MORE_FILES)
      {
      this->Error = UnknownError;
      }
    if (h != INVALID_HANDLE_VALUE)
      {
      FindClose(h);
      }
    }
#else
  errno = 0;
  DIR* dir = opendir(dirname);

  if (!dir)
    {
    int e = errno;
    if (e == EACCES || e == EPERM)
      {
      this->Error = AccessDenied;
      }
    else if (e == ENOENT || e == ENOTDIR)
      {
      this->Error = FileNotFound;
      }
    else
      {
      this->Error = UnknownError;
      }
    }
  else
    {
    for (redef_dirent *d = readdir(dir); d; d = readdir(dir))
      {
      if (strcmp(d->d_name, ".") != 0 && strcmp(d->d_name, "..") != 0)
        {
        this->AddEntry(d->d_name, 0, 0);
        }
      }
    closedir(dir);
    }
#endif
}

//----------------------------------------------------------------------------
vtkDICOMFileDirectory::~vtkDICOMFileDirectory()
{
  delete [] this->Entries;
}

//----------------------------------------------------------------------------
const char *vtkDICOMFileDirectory::GetFile(int i)
{
  if (i < 0 || i >= this->NumberOfFiles)
    {
    return 0;
    }
  return this->Entries[i].Name.c_str();
}

//----------------------------------------------------------------------------
bool vtkDICOMFileDirectory::IsDirectory(int i)
{
  if (i < 0 || i >= this->NumberOfFiles)
    {
    return false;
    }
#ifndef _WIN32
  if ((this->Entries[i].Mask & TypeDirectory) == 0)
    {
    struct stat fs;
    vtkDICOMFilePath path(this->Name);
    path.PushBack(this->Entries[i].Name);
    if (stat(path.AsString().c_str(), &fs) == 0)
      {
      if (S_ISDIR(fs.st_mode))
        {
        this->Entries[i].Flags |= TypeDirectory;
        }
      this->Entries[i].Mask |= TypeDirectory;
      }
    }
#endif
  return ((this->Entries[i].Flags & TypeDirectory) != 0);
}

//----------------------------------------------------------------------------
bool vtkDICOMFileDirectory::IsSymlink(int i)
{
  if (i < 0 || i >= this->NumberOfFiles)
    {
    return false;
    }
  if ((this->Entries[i].Mask & TypeSymlink) == 0)
    {
    this->StatEntry(i);
    }

  return ((this->Entries[i].Flags & TypeSymlink) != 0);
}

//----------------------------------------------------------------------------
bool vtkDICOMFileDirectory::IsHidden(int i)
{
  if (i < 0 || i >= this->NumberOfFiles)
    {
    return false;
    }
  if ((this->Entries[i].Mask & TypeHidden) == 0)
    {
    this->StatEntry(i);
    }

  return ((this->Entries[i].Flags & TypeHidden) != 0);
}

//----------------------------------------------------------------------------
void vtkDICOMFileDirectory::AddEntry(
  const char *name, unsigned short flags, unsigned short mask)
{
  int n = this->NumberOfFiles;
  if (this->Entries == 0)
    {
    this->Entries = new Entry[4];
    }
  else if (n >= 4 && ((n-1) & n) == 0)
    {
    Entry *entries = new Entry[n*2];
    for (int i = 0; i < n; i++)
      {
      entries[i] = this->Entries[i];
      }
    delete [] this->Entries;
    this->Entries = entries;
    }

  this->Entries[n].Name = name;
  this->Entries[n].Flags = flags;
  this->Entries[n].Mask= mask;

  this->NumberOfFiles++;
}

//----------------------------------------------------------------------------
#ifndef _WIN32
void vtkDICOMFileDirectory::StatEntry(int i)
{
  if (i >= 0 && i < this->NumberOfFiles)
    {
    struct stat fs;
    vtkDICOMFilePath path(this->Name);
    path.PushBack(this->Entries[i].Name);
    if (lstat(path.AsString().c_str(), &fs) == 0)
      {
      if (S_ISLNK(fs.st_mode))
        {
        this->Entries[i].Flags |= TypeSymlink;
        }
      this->Entries[i].Mask |= TypeSymlink;
#ifdef __APPLE__
      if ((fs.st_flags & UF_HIDDEN) != 0)
        {
        this->Entries[i].Flags |= TypeHidden;
        }
      this->Entries[i].Mask |= TypeHidden;
#endif
      }
    }
}
#else /* _WIN32 */
void vtkDICOMFileDirectory::StatEntry(int)
{
  // on Windows, the attributes were collected when the
  // directory listing was done in the constructor, so
  // there is no need to run stat on the files
}
#endif

//----------------------------------------------------------------------------
int vtkDICOMFileDirectory::Create(const char *name)
{
  int result = 0;

  std::vector<std::string> dirsToCreate;
  vtkDICOMFilePath path(name);

  while (!path.IsRoot() && !path.IsEmpty() && result == 0)
    {
    int code = vtkDICOMFile::Access(path.AsString().c_str(), vtkDICOMFile::In);
    if (code == 0 || code == vtkDICOMFile::AccessDenied)
      {
      // The name exists as a file, or there is no permission to use path
      result = AccessDenied;
      }
    else if (code == vtkDICOMFile::FileIsDirectory)
      {
      // Found a directory!
      break;
      }
    dirsToCreate.push_back(path.AsString());
    path.PopBack();
    }

  while (dirsToCreate.size() > 0 && result == 0)
    {
    const std::string& dirname = dirsToCreate.back();
#ifdef _WIN32
    vtkDICOMFilePath dirpath(dirname);
    const wchar_t *widename = dirpath.Wide();
    if (widename == 0)
      {
      result = UnknownError;
      }
    else if (!CreateDirectoryW(widename, NULL))
      {
      DWORD e = GetLastError();
      if (e == ERROR_ACCESS_DENIED ||
          e == ERROR_ALREADY_EXISTS)
        {
        result = AccessDenied;
        }
      else if (e == ERROR_PATH_NOT_FOUND)
        {
        result = ImpossiblePath;
        }
      else if (e == ERROR_DISK_FULL)
        {
        result = OutOfSpace;
        }
      else
        {
        result = UnknownError;
        }
      }
#else
    if (mkdir(dirname.c_str(), 00777) != 0)
      {
      int e = errno;
      if (e == EACCES || e == EPERM)
        {
        result = AccessDenied;
        }
      else if (e == ENOENT || e == ENOTDIR)
        {
        result = ImpossiblePath;
        }
      else if (e == ENOSPC)
        {
        // some systems also have EDQUOT for "quota exceeded"
        result = OutOfSpace;
        }
      else
        {
        result = UnknownError;
        }
      }
#endif
    dirsToCreate.pop_back();
    }

  return result;
}
