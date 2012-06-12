#include "vtkDICOMMetaData.h"

#include <vtkObjectFactory.h>
#include <vtkSmartPointer.h>
#include <vtkMatrix4x4.h>

#include <vector>
#include <utility>

vtkStandardNewMacro(vtkDICOMMetaData);

// The hash table size, must be a power of two
#define METADATA_HASH_SIZE 512

//----------------------------------------------------------------------------
// An Element is just a std::pair of smart pointers
// (smart pointers provide automatic reference counting)
struct vtkDICOMMetaData::Element
{
  Element(Tag t, unsigned short r, unsigned int l, int d)
    : tag(t), vr(r), vl(l), data(d) {}

  Tag tag;
  unsigned short vr;
  size_t vl;
  int data;
};

//----------------------------------------------------------------------------
// Constructor
vtkDICOMMetaData::vtkDICOMMetaData()
{
  this->Table = NULL;
}

// Destructor
vtkDICOMMetaData::~vtkDICOMMetaData()
{
  Element ***htable = this->Table;

  if (htable)
    {
    for (unsigned int i = 0; i < METADATA_HASH_SIZE; i++)
      {
      delete [] htable[i];
      }
    delete [] htable;
    }

  this->Table = NULL;
}

// Get an element from the hash table.
vtkDICOMMetaData::Element *vtkDICOMMetaData::FindElement(Tag tag)
{
  unsigned int m = METADATA_HASH_SIZE - 1;
  unsigned int i = (tag.GetHash() & m);
  vtkDICOMMetaData::Element ***htable = this->Table;
  vtkDICOMMetaData::Element **hptr;

  if (htable && (hptr = htable[i]) != NULL)
    {
    while (*hptr)
      {
      if ((*hptr)->tag == tag)
        {
        return *hptr;
        }
      hptr++;
      }
    }

  return NULL;
}

// Erase an element from the hash table
void vtkDICOMMetaData::EraseElement(Tag tag)
{
  unsigned int m = METADATA_HASH_SIZE - 1;
  unsigned int i = (tag.GetHash() & m);
  vtkDICOMMetaData::Element ***htable = this->Table;
  vtkDICOMMetaData::Element **hptr;

  if (htable && (hptr = htable[i]) != NULL)
    {
    while (*hptr)
      {
      if ((*hptr)->tag == tag)
        {
        delete *hptr;
        *hptr = NULL;
        break;
        }
      }
    }
}

// Return a reference to the element within the hash table, which can
// be used to insert a new value.
vtkDICOMMetaData::Element *&vtkDICOMMetaData::FindElementSlot(Tag tag)
{
  unsigned int m = METADATA_HASH_SIZE - 1;
  unsigned int i = (tag.GetHash() & m);
  vtkDICOMMetaData::Element ***htable = this->Table;
  vtkDICOMMetaData::Element **hptr;

  if (htable == NULL)
    {
    // allocate the hash table
    m = METADATA_HASH_SIZE;
    htable = new vtkDICOMMetaData::Element **[METADATA_HASH_SIZE];
    this->Table = htable;
    do { *htable++ = NULL; } while (--m);
    htable = this->Table;
    }

  hptr = htable[i];

  if (hptr == NULL)
    {
    hptr = new vtkDICOMMetaData::Element *[4];
    htable[i] = hptr;
    hptr[0] = NULL;
    hptr[1] = NULL;
    }
  else if (*hptr)
    {
    // see if item is already there
    unsigned int n = 0;
    do
      {
      if ((*hptr)->tag == tag)
        {
        break;
        }
      n++;
      hptr++;
      }
    while (*hptr);

    if (*hptr == NULL)
      {
      // if n+1 is a power of two, double allocated space
      if (n > 1 && (n & (n+1)) == 0)
        {
        vtkDICOMMetaData::Element **oldptr = htable[i];
        hptr = new vtkDICOMMetaData::Element *[2*(n+1)];
        htable[i] = hptr;
        for (unsigned int j = 0; j < n; j++)
          {
          *hptr++ = *oldptr++;
          }
        delete [] oldptr;
        }

      // add a terminating null
      hptr[1] = NULL;
      }
    }

  return *hptr;
}

// Insert an element into the hash table
void vtkDICOMMetaData::InsertElement(Tag tag, unsigned short vr, unsigned int vl, int data)
{
  vtkDICOMMetaData::Element *&slot = this->FindElementSlot(tag);

  if (slot)
    {
    delete slot;
    }

  slot = new vtkDICOMMetaData::Element(tag, vr, vl, data);
}

void vtkDICOMMetaData::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}