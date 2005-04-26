#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "chplmem.h"
#include "chplrt.h"
#include "chpltypes.h"

#undef malloc
#undef calloc
#undef free
#undef realloc

#define HASHSIZE 1019

typedef struct _memTableEntry { /* table entry */
  size_t number;
  size_t size;
  char* description;
  void* memAlloc;

  struct _memTableEntry* nextInBucket;
  struct _memTableEntry* prevInstalled;
  struct _memTableEntry* nextInstalled;

} memTableEntry;


/* hash table */
static memTableEntry* memTable[HASHSIZE];
static memTableEntry* first = NULL;
static memTableEntry* last = NULL;


/* hashing function */
static unsigned hash(void* memAlloc) {
  unsigned hashValue = 0;
  char* fakeCharPtr = (char*)&memAlloc;
  size_t i;
  for (i = 0; i < sizeof(void*); i++) {
    hashValue = *fakeCharPtr + 31 * hashValue;
    fakeCharPtr++;
  }
  return hashValue % HASHSIZE;
}


static int memmax = 0;
static int memtable = 0;
static int memthreshold = 0;
static FILE* memlog = NULL;
static _integer64 memmaxValue = 0;
static _integer64 memthresholdValue = 0;


void initMemTable(void) {
  if (memtable) {
    int i;
    for (i = 0; i < HASHSIZE; i++) {
      memTable[i] = NULL;
    }
  }
}


void setMemmax(_integer64 value) {
  memmax = 1;
  memmaxValue = value;
}


void setMemtable(void) {
  memtable = 1;
}


void setMemthreshold(_integer64 value) {
  if (!memlog) {
    fprintf(stderr, "--memthreshold useless when used without --memtrace\n");
    exit(0);
  }
  memthreshold = 1;
  memthresholdValue = value;
}


void setMemtrace(char* memlogname) {
  if (memlogname) {
    memlog = fopen(memlogname, "w");
    if (!memlog) {
      fprintf(stderr, "***Error:  Unable to open \"%s\"***\n", memlogname);
      exit(0);
    }
  } 
}


void printMemTable(void) {
  if (!memtable) {
    fprintf(stderr, "printMemTable() only works with the --memtable flag\n");
    exit(0);
  }
  memTableEntry* memEntry = NULL;
  fprintf(stdout, "\n");

  int numberWidth   = 9;
  int addressWidth  = 12;
  int precision     = 8;

  char* size        = "Size:";
  char* bytes       = "(bytes)"; 
  char* number      = "Number:";
  char* total       = "Total:";
  char* address     = "Address:";
  char* description = "Description:";
  char* line40      = "========================================";   

  fprintf(stdout, "%s%s\n", line40, line40);

  fprintf(stdout, "----------------------\n");
  fprintf(stdout, "***Allocated Memory***\n");
  fprintf(stdout, "----------------------\n");

  fprintf(stdout, "%-*s%-*s%-*s%-*s%-s\n", 
          numberWidth, size, 
          numberWidth, number, 
          numberWidth, total, 
          addressWidth, address, 
          description);

  fprintf(stdout, "%-*s%-*s%-*s\n", 
          numberWidth, bytes, 
          numberWidth, "", 
          numberWidth, bytes);

  fprintf(stdout, "%s%s\n", line40, line40);

  for (memEntry = first;
       memEntry != NULL;
       memEntry = memEntry->nextInstalled) {
    
    size_t chunk = memEntry->number * memEntry->size;
    fprintf(stdout, "%-*u%-*u%-*u%#-*.*x%-s\n", 
            numberWidth, (unsigned)memEntry->size, 
            numberWidth, (unsigned)memEntry->number, 
            numberWidth, (unsigned)chunk, 
            addressWidth, precision, (unsigned)(intptr_t)memEntry->memAlloc, 
            memEntry->description);
  }
  fprintf(stdout, "\n");
}


static memTableEntry* lookupMemory(void* memAlloc) {
  memTableEntry* memEntry = NULL;
  unsigned hashValue = hash(memAlloc);

  for (memEntry = memTable[hashValue]; 
       memEntry != NULL; 
       memEntry = memEntry->nextInBucket) {

    if (memEntry->memAlloc == memAlloc) {
      return memEntry;
    }
  }
  return NULL;
}


static void installMemory(void* memAlloc, size_t number, size_t size, 
                   char* description) {
  unsigned hashValue;
  memTableEntry* memEntry = lookupMemory(memAlloc);

  if (!memEntry) { 
    memEntry = (memTableEntry*) calloc(1, sizeof(memTableEntry));
    if (!memEntry) {
      fprintf(stderr, "***Error:  Out of memory allocating table entry for %s"
              "***\n", description);
      exit(0);
    }

    hashValue = hash(memAlloc);
    memEntry->nextInBucket = memTable[hashValue];
    memTable[hashValue] = memEntry;

    if (first == NULL) {
      first = memEntry;
    } else {
      last->nextInstalled = memEntry;
      memEntry->prevInstalled = last;
    }
    last = memEntry;

    memEntry->description = (char*) malloc((strlen(description) + 1)
                                           * sizeof(char));
    if (!memEntry->description) {
      fprintf(stderr, "***Error:  Out of memory allocating table entry for %s"
              "***\n", description);
      exit(0);
    }
    strcpy(memEntry->description, description);
    memEntry->memAlloc = memAlloc;
  }  
  memEntry->number = number;
  memEntry->size = size;
}


static void updateMemory(memTableEntry* memEntry, void* newAddress, 
                         size_t number, size_t size) {
  memEntry->memAlloc = newAddress;
  memEntry->number = number;
  memEntry->size = size;
}


static void removeMemory(void* memAlloc) {
  memTableEntry* memEntry = lookupMemory(memAlloc);

  if (memEntry) {
    /* Remove the entry from the first-to-last list. */
    if (memEntry == first) {
      first = memEntry->nextInstalled;
      if (memEntry->nextInstalled) {
        memEntry->nextInstalled->prevInstalled = NULL;
      }
    } else {
      memEntry->prevInstalled->nextInstalled = memEntry->nextInstalled;
      if (memEntry->nextInstalled) {
        memEntry->nextInstalled->prevInstalled = memEntry->prevInstalled;
      } else {
        last = memEntry->prevInstalled;
      } 
    }

    /* Remove the entry from the bucket list. */
    unsigned hashValue = hash(memAlloc);    
    memTableEntry* thisBucketEntry = memTable[hashValue];
    
    if (thisBucketEntry->memAlloc == memAlloc) {
      memTable[hashValue] = thisBucketEntry->nextInBucket;
    } else {
      for (thisBucketEntry = memTable[hashValue]; 
           thisBucketEntry != NULL; 
           thisBucketEntry = thisBucketEntry->nextInBucket) {

        memTableEntry* nextBucketEntry = thisBucketEntry->nextInBucket;
        
        if (nextBucketEntry->memAlloc == memAlloc) {
          thisBucketEntry->nextInBucket = nextBucketEntry->nextInBucket;
          thisBucketEntry = nextBucketEntry;
          break;
        }
      }
    }
    free(thisBucketEntry->description);
    free(thisBucketEntry);
  } else {
    fprintf(stderr, "***Error:  Attempting to free memory that wasn't "
            "allocated***\n");
    exit(0);
  }
}


static void confirm(void* memAlloc, char* description) {
  if (!memAlloc) {
    fprintf(stderr, "***Error:  Out of memory allocating %s***\n", 
            description);
    exit(0);
  }
}


static void printToMemLog(size_t number, size_t size, char* description, 
                          char* memType, void* memAlloc, void* moreMemAlloc) {
  size_t chunk = number * size;
  
  if (chunk >= memthresholdValue) {
    if (moreMemAlloc && (moreMemAlloc != memAlloc)) {
      fprintf(memlog, "%s called for %u items of size %u"
              " for %s:  0x%x -> 0x%x\n", memType, (unsigned)number, 
              (unsigned)size, description, (unsigned)(intptr_t)memAlloc, 
              (unsigned)(intptr_t)moreMemAlloc);     
    } else {
      fprintf(memlog, "%s called for %u items of size %u"
              " for %s:  0x%x\n", memType, (unsigned)number, (unsigned)size, 
              description, (unsigned)(intptr_t)memAlloc);       
    }
  }
}


void* _chpl_malloc(size_t number, size_t size, char* description) {
  size_t chunk = number * size;
  void* memAlloc = malloc(chunk);
  confirm(memAlloc, description);
  if (memtable) {
    installMemory(memAlloc, number, size, description);  
  }
  if (memlog) {
    printToMemLog(number, size, description, "malloc", memAlloc, NULL);
  }
  return memAlloc;
}


void* _chpl_calloc(size_t number, size_t size, char* description) {
  void* memAlloc = calloc(number, size);
  confirm(memAlloc, description);
  if (memtable) {
    installMemory(memAlloc, number, size, description);
  }
  if (memlog) {
    printToMemLog(number, size, description, "calloc", memAlloc, NULL);
  }
  return memAlloc;
}


void _chpl_free(void* memAlloc) {
  if (memtable) {
    removeMemory(memAlloc);
  }
  free(memAlloc);
}


void* _chpl_realloc(void* memAlloc, size_t number, size_t size, 
                    char* description) {
  size_t chunk = number * size;
  if (!chunk) {
    _chpl_free(memAlloc);
    return NULL;
  }

  memTableEntry* memEntry;
  if (memtable) {
    memEntry = lookupMemory(memAlloc);
    if (!memEntry && (memAlloc != NULL)) {
      fprintf(stderr, "***Error:  Attempting to realloc memory for %s that "
              "wasn't allocated***\n", description);
      exit(0);
    }
  }

  void* moreMemAlloc = realloc(memAlloc, chunk);
  confirm(moreMemAlloc, description);

  if (memtable) {
    if ((memAlloc != NULL)  && (moreMemAlloc != memAlloc)) {
      if (memEntry) {
        updateMemory(memEntry, moreMemAlloc, number, size);
      }
    } else {
      installMemory(moreMemAlloc, number, size, description);
    }
  }

  if (memlog) {
    printToMemLog(number, size, description, "realloc", memAlloc, 
                  moreMemAlloc);
  }
  return moreMemAlloc;
}
