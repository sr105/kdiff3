/***************************************************************************
                          diff.cpp  -  description
                             -------------------
    begin                : Mon Mar 18 2002
    copyright            : (C) 2002-2004 by Joachim Eibl
    email                : joachim.eibl@gmx.de
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <stdio.h>
#include <iostream>

#include "diff.h"
#include "fileaccess.h"
#include "optiondialog.h"

#include <kmessagebox.h>
#include <klocale.h>
#include <qfileinfo.h>
#include <qdir.h>

#include <map>
#include <assert.h>
#include <ctype.h>
//using namespace std;


int LineData::width() const
{
   int w=0;
   int j=0;
   for( int i=0; i<size; ++i )
   {
      if ( pLine[i]=='\t' )
      {
         for(j %= g_tabSize; j<g_tabSize; ++j)
            ++w;
         j=0;
      }
      else
      {
         ++w;
         ++j;
      }
   }
   return w;
}


// The bStrict flag is true during the test where a nonmatching area ends.
// Then the equal()-function requires that the match has more than 2 nonwhite characters.
// This is to avoid matches on trivial lines (e.g. with white space only).
// This choice is good for C/C++.
bool equal( const LineData& l1, const LineData& l2, bool bStrict )
{
   if ( l1.pLine==0 || l2.pLine==0) return false;

   if ( bStrict && g_bIgnoreTrivialMatches && (l1.occurances>=5 || l2.occurances>=5) )
      return false;

   // Ignore white space diff
   const char* p1 = l1.pLine;
   const char* p1End = p1 + l1.size;

   const char* p2 = l2.pLine;
   const char* p2End = p2 + l2.size;

   if ( g_bIgnoreWhiteSpace )
   {
      int nonWhite = 0;
      for(;;)
      {
         while( isWhite( *p1 ) && p1!=p1End ) ++p1;
         while( isWhite( *p2 ) && p2!=p2End ) ++p2;

         if ( p1 == p1End  &&  p2 == p2End )
         {
            if ( bStrict && g_bIgnoreTrivialMatches )
            {  // Then equality is not enough
               return nonWhite>2;
            }
            else  // equality is enough
               return true;
         }
         else if ( p1 == p1End || p2 == p2End )
            return false;

         if( *p1 != *p2 )
            return false;
         ++p1;
         ++p2;
         ++nonWhite;
      }
   }

   else
   {
      if ( l1.size==l2.size && memcmp(p1, p2, l1.size)==0)
         return true;
      else
         return false;
   }
}




static bool isLineOrBufEnd( const char* p, int i, int size )
{
   return 
      i>=size        // End of file
      || p[i]=='\n'  // Normal end of line

      // No support for Mac-end of line yet, because incompatible with GNU-diff-routines.      
      // || ( p[i]=='\r' && (i>=size-1 || p[i+1]!='\n') 
      //                 && (i==0        || p[i-1]!='\n') )  // Special case: '\r' without '\n'
      ;
}


/* Features of class SourceData:
- Read a file (from the given URL) or accept data via a string.
- Allocate and free buffers as necessary.
- Run a preprocessor, when specified.
- Run the line-matching preprocessor, when specified.
- Run other preprocessing steps: Uppercase, ignore comments, 
                                 remove carriage return, ignore numbers.

Order of operation:
 1. If data was given via a string then save it to a temp file. (see setData())
 2. If the specified file is nonlocal (URL) copy it to a temp file.
 3. If a preprocessor was specified, run the input file through it.
 4. Read the output of the preprocessor.
 5. If Uppercase was specified: Turn the read data to uppercase.
 6. Write the result to a temp file.
 7. If a line-matching preprocessor was specified, run the temp file through it.
 8. Read the output of the line-matching preprocessor.
 9. If ignore numbers was specified, strip the LMPP-output of all numbers.
10. If ignore comments was specified, strip the LMPP-output of comments.

Optimizations: Skip unneeded steps.
*/

SourceData::SourceData()
{ 
   m_pOptionDialog = 0;
   reset();
}

SourceData::~SourceData()
{
   reset();
}

void SourceData::reset()
{
   m_normalData.reset();
   m_lmppData.reset();
   if ( !m_tempInputFileName.isEmpty() )
   {
      FileAccess::removeFile( m_tempInputFileName );
      m_tempInputFileName = "";
   }
}

void SourceData::setFilename( const QString& filename )
{
   if (filename.isEmpty())
   {
      reset();
   }
   else
   {
      FileAccess fa( filename );
      setFileAccess( fa );
   }
}

bool SourceData::isEmpty() 
{ 
   return getFilename().isEmpty(); 
}

bool SourceData::hasData() 
{ 
   return m_normalData.m_pBuf != 0;
}

void SourceData::setOptionDialog( OptionDialog* pOptionDialog )
{
   m_pOptionDialog = pOptionDialog;
}

QString SourceData::getFilename()
{
   return m_fileAccess.absFilePath();
}

QString SourceData::getAliasName()
{
   return m_aliasName.isEmpty() ? m_fileAccess.prettyAbsPath() : m_aliasName;
}

void SourceData::setAliasName( const QString& name )
{
   m_aliasName = name;
}

void SourceData::setFileAccess( const FileAccess& fileAccess )
{
   m_fileAccess = fileAccess;
   m_aliasName = QString();
   if ( !m_tempInputFileName.isEmpty() )
   {
      FileAccess::removeFile( m_tempInputFileName );
      m_tempInputFileName = "";
   }
}

void SourceData::setData( const QString& data )
{
   // Create a temp file for preprocessing:
   if ( m_tempInputFileName.isEmpty() )
   {
      m_tempInputFileName = FileAccess::tempFileName();
   }
   
   FileAccess f( m_tempInputFileName );
   bool bSuccess = f.writeFile( encodeString(data, m_pOptionDialog), data.length() );
   if ( !bSuccess )
   {
      KMessageBox::error( m_pOptionDialog, i18n("Writing clipboard data to temp file failed.") );
      return;
   }
   
   m_aliasName = i18n("From Clipboard");
   m_fileAccess = FileAccess("");  // Effect: m_fileAccess.isValid() is false
}

const LineData* SourceData::getLineDataForDiff() const 
{
   return m_lmppData.m_pBuf==0 ? &m_normalData.m_v[0] : &m_lmppData.m_v[0];
}

const LineData* SourceData::getLineDataForDisplay() const
{
   return &m_normalData.m_v[0];
}

int  SourceData::getSizeLines() const
{
   return m_normalData.m_vSize;
}

int SourceData::getSizeBytes() const
{
   return m_normalData.m_size;
}

const char* SourceData::getBuf() const
{
   return m_normalData.m_pBuf;
}

bool SourceData::isText()
{
   return m_normalData.m_bIsText;
}
 
bool SourceData::isFromBuffer()
{
   return !m_fileAccess.isValid();
}


bool SourceData::isBinaryEqualWith( const SourceData& other ) const
{
   return getSizeBytes() == other.getSizeBytes() &&  memcmp( getBuf(), other.getBuf(), getSizeBytes() )==0;
}

void SourceData::FileData::reset()
{
   delete (char*)m_pBuf;
   m_pBuf = 0;
   m_v.clear();
   m_size = 0;
   m_vSize = 0;
   m_bIsText = true;
}

bool SourceData::FileData::readFile( const QString& filename )
{
   reset();
   if ( filename.isEmpty() )   { return true; }

   FileAccess fa( filename );
   m_size = fa.sizeForReading();
   char* pBuf;
   m_pBuf = pBuf = new char[m_size+100];  // Alloc 100 byte extra: Savety hack, not nice but does no harm.
   bool bSuccess = fa.readFile( pBuf, m_size );
   if ( !bSuccess )
   {
      delete pBuf;
      m_pBuf = 0;
      m_size = 0;
   }
   return bSuccess;
}

bool SourceData::saveNormalDataAs( const QString& fileName )
{
   return m_normalData.writeFile( fileName );
}

bool SourceData::FileData::writeFile( const QString& filename )
{
   if ( filename.isEmpty() )   { return true; }

   FileAccess fa( filename );
   bool bSuccess = fa.writeFile(m_pBuf, m_size);
   return bSuccess;
}

void SourceData::FileData::copyBufFrom( const FileData& src )
{
   reset();
   char* pBuf;   
   m_size = src.m_size;
   m_pBuf = pBuf = new char[m_size+100];
   memcpy( pBuf, src.m_pBuf, m_size );
}

void SourceData::readAndPreprocess()
{
   QString fileNameIn1;
   QString fileNameOut1;
   QString fileNameIn2;
   QString fileNameOut2;
   
   bool bTempFileFromClipboard = !m_fileAccess.isValid();
      
   // Detect the input for the preprocessing operations
   if ( !bTempFileFromClipboard )
   {
      if ( m_fileAccess.isLocal() )
      {
         fileNameIn1 = m_fileAccess.absFilePath();
      }
      else    // File is not local: create a temporary local copy:
      {
         if ( m_tempInputFileName.isEmpty() )  { m_tempInputFileName = FileAccess::tempFileName(); }
      
         m_fileAccess.copyFile(m_tempInputFileName);
         fileNameIn1 = m_tempInputFileName;
      }
   }
   else // The input was set via setData(), probably from clipboard.
   {
      fileNameIn1 = m_tempInputFileName;
   }
      
   m_normalData.reset();
   m_lmppData.reset();
   
   FileAccess faIn(fileNameIn1);
   int fileInSize = faIn.size();
   
   if ( fileInSize > 0 )
   {  

#ifdef _WIN32
      QString catCmd = "type";
      fileNameIn1.replace( '/', "\\" );
#else
      QString catCmd = "cat";
#endif
      
      // Run the first preprocessor
      if ( m_pOptionDialog->m_PreProcessorCmd.isEmpty() )
      {
         // No preprocessing: Read the file directly:
         m_normalData.readFile( fileNameIn1 );
      }
      else
      {
         QString ppCmd = m_pOptionDialog->m_PreProcessorCmd;
         fileNameOut1 = FileAccess::tempFileName();
         QString cmd = catCmd + " \"" + fileNameIn1 + "\" | " + ppCmd  + " >\"" + fileNameOut1+"\"";
         ::system( encodeString(cmd, m_pOptionDialog) );
         bool bSuccess = m_normalData.readFile( fileNameOut1 );
         if ( fileInSize >0 && ( !bSuccess || m_normalData.m_size==0 ) )
         {
            KMessageBox::error(m_pOptionDialog, 
               i18n("Preprocessing possibly failed. Check this command:\n\n  %1"
                  "\n\nThe preprocessing command will be disabled now."
               ).arg(cmd) );
            m_pOptionDialog->m_PreProcessorCmd = "";
            m_normalData.readFile( fileNameIn1 );
         }
      }

      // Internal Preprocessing: Uppercase-conversion   
      bool bInternalPreprocessing = false;
      if ( m_pOptionDialog->m_bUpCase )
      {
         int i;
         char* pBuf = const_cast<char*>(m_normalData.m_pBuf);
         for(i=0; i<m_normalData.m_size; ++i)
         {
            pBuf[i] = toupper(pBuf[i]);
         }
         
         bInternalPreprocessing = true;
      }
      
      // LineMatching Preprocessor
      if ( ! m_pOptionDialog->m_LineMatchingPreProcessorCmd.isEmpty() )
      {
         if ( bInternalPreprocessing )  
         {
            // write data to file after internal preprocessing before running the external LMPP-cmd.
            if ( !fileNameOut1.isEmpty() )
            {
               FileAccess::removeFile( fileNameOut1 );
               fileNameOut1="";
            }
            
            fileNameIn2 = FileAccess::tempFileName();
            bool bSuccess = m_normalData.writeFile( fileNameIn2 );
            if ( !bSuccess )
            {
               KMessageBox::error(m_pOptionDialog, i18n("Error writing temporary file: %1").arg(fileNameIn2) );
            }
         }
         else
         {
            fileNameIn2 = fileNameOut1.isEmpty() ? fileNameIn1 : fileNameOut1;
         }
      
         QString ppCmd = m_pOptionDialog->m_LineMatchingPreProcessorCmd;
         fileNameOut2 = FileAccess::tempFileName();
         QString cmd = catCmd + " \"" + fileNameIn2 + "\" | " + ppCmd  + " >\"" + fileNameOut2 + "\"";
         ::system( encodeString(cmd, m_pOptionDialog) );
         bool bSuccess = m_lmppData.readFile( fileNameOut2 );
         if ( FileAccess(fileNameIn2).size()>0 && ( !bSuccess || m_lmppData.m_size==0 ) )
         {
            KMessageBox::error(m_pOptionDialog, 
               i18n("The line-matching-preprocessing possibly failed. Check this command:\n\n  %1"
                    "\n\nThe line-matching-preprocessing command will be disabled now."
                   ).arg(cmd) );
            m_pOptionDialog->m_LineMatchingPreProcessorCmd = "";
            m_lmppData.readFile( fileNameIn2 );
         }
         FileAccess::removeFile( fileNameOut2 );
         
         if ( bInternalPreprocessing && !fileNameIn2.isEmpty() )
         {
            FileAccess::removeFile( fileNameIn2 );
            fileNameIn2="";
         }
      }
      else if ( m_pOptionDialog->m_bIgnoreComments )
      {
         // We need a copy of the normal data.
         m_lmppData.copyBufFrom( m_normalData );
      }
      else
      {  // We don't need any lmpp data at all.
         m_lmppData.reset();
      }
   }            
   
   m_normalData.preprocess( m_pOptionDialog->m_bPreserveCarriageReturn );
   m_lmppData.preprocess( false );
   
   if ( m_lmppData.m_vSize < m_normalData.m_vSize )
   {
      // This probably is the fault of the LMPP-Command, but not worth reporting.
      m_lmppData.m_v.resize( m_normalData.m_vSize );
      for(int i=m_lmppData.m_vSize; i<m_normalData.m_vSize; ++i )
      {  // Set all empty lines to point to the end of the buffer.
         m_lmppData.m_v[i].pLine = m_lmppData.m_pBuf+m_lmppData.m_size;
      }
      
      m_lmppData.m_vSize = m_normalData.m_vSize;
   }
   
   // Ignore comments
   if ( m_pOptionDialog->m_bIgnoreComments )
   {
      m_lmppData.removeComments();
      int vSize = min2(m_normalData.m_vSize, m_lmppData.m_vSize);
      for(int i=0; i<vSize; ++i )
      {
         m_normalData.m_v[i].bContainsPureComment = m_lmppData.m_v[i].bContainsPureComment;
      }
   }
      
   // Remove unneeded temporary files. (A temp file from clipboard must not be deleted.)
   if ( !bTempFileFromClipboard && !m_tempInputFileName.isEmpty() )
   {
      FileAccess::removeFile( m_tempInputFileName );
      m_tempInputFileName = "";
   }
   
   if ( !fileNameOut1.isEmpty() )
   {
      FileAccess::removeFile( fileNameOut1 );
      fileNameOut1="";
   }
}


/** Prepare the linedata vector for every input line.*/
void SourceData::FileData::preprocess( bool bPreserveCR )
{
   const char* p = m_pBuf;
   m_bIsText = true;
   int lines = 1;
   int i;
   for( i=0; i<m_size; ++i )
   {
      if ( isLineOrBufEnd(p,i,m_size) )
      {
         ++lines;
      }
      if ( p[i]=='\0' )
      {
         m_bIsText = false;
      }
   }

   m_v.resize( lines+5 );
   int lineIdx=0;
   int lineLength=0;
   bool bNonWhiteFound = false;
   int whiteLength = 0;
   for( i=0; i<=m_size; ++i )
   {
      if ( isLineOrBufEnd( p, i, m_size ) )
      {
         m_v[lineIdx].pLine = &p[ i-lineLength ];
         while ( !bPreserveCR  &&  lineLength>0  &&  m_v[lineIdx].pLine[lineLength-1]=='\r'  )
         {
            --lineLength;
         }
         m_v[lineIdx].pFirstNonWhiteChar = m_v[lineIdx].pLine + min2(whiteLength,lineLength);
         m_v[lineIdx].size = lineLength;
         lineLength = 0;
         bNonWhiteFound = false;
         whiteLength = 0;
         ++lineIdx;
      }
      else
      {
         ++lineLength;

         if ( ! bNonWhiteFound && isWhite( p[i] ) )
            ++whiteLength;
         else
            bNonWhiteFound = true;
      }
   }
   assert( lineIdx == lines );

   m_vSize = lines;
}


// Must not be entered, when within a comment.
// Returns either at a newline-character p[i]=='\n' or when i==size.
// A line that contains only comments is still "white".
// Comments in white lines must remain, while comments in
// non-white lines are overwritten with spaces.
static void checkLineForComments(
   char* p,   // pointer to start of buffer
   int& i,    // index of current position (in, out)
   int size,  // size of buffer
   bool& bWhite,          // false if this line contains nonwhite characters (in, out)
   bool& bCommentInLine,  // true if any comment is within this line (in, out)
   bool& bStartsOpenComment  // true if the line ends within an comment (out)
   )
{
   bStartsOpenComment = false;
   for(; i<size; ++i )
   {
      // A single apostroph ' has prio over a double apostroph " (e.g. '"')
      // (if not in a string)
      if ( p[i]=='\'' )
      {
         bWhite = false;
         ++i;
         for( ; !isLineOrBufEnd(p,i,size) && p[i]!='\''; ++i)
            ;
         if (p[i]=='\'') ++i;
      }

      // Strings have priority over comments: e.g. "/* Not a comment, but a string. */"
      else if ( p[i]=='"' )
      {
         bWhite = false;
         ++i;
         for( ; !isLineOrBufEnd(p,i,size) && !(p[i]=='"' && p[i-1]!='\\'); ++i)
            ;
         if (p[i]=='"') ++i;
      }

      // C++-comment
      else if ( p[i]=='/' && i+1<size && p[i+1] =='/' )
      {
         int commentStart = i;
         bCommentInLine = true;
         i+=2;
         for( ; !isLineOrBufEnd(p,i,size); ++i)
            ;
         if ( !bWhite )
         {
            memset( &p[commentStart], ' ', i-commentStart );
         }
         return;
      }

      // C-comment
      else if ( p[i]=='/' && i+1<size && p[i+1] =='*' )
      {
         int commentStart = i;
         bCommentInLine = true;
         i+=2;
         for( ; !isLineOrBufEnd(p,i,size); ++i)
         {
            if ( i+1<size && p[i]=='*' && p[i+1]=='/')  // end of the comment
            {
               i+=2;

               // More comments in the line?
               checkLineForComments( p, i, size, bWhite, bCommentInLine, bStartsOpenComment );
               if ( !bWhite )
               {
                  memset( &p[commentStart], ' ', i-commentStart );
               }
               return;
            }
         }
         bStartsOpenComment = true;
         return;
      }


      if ( isLineOrBufEnd(p,i,size) )
      {
         return;
      }
      else if ( !isspace(p[i]) )
      {
         bWhite = false;
      }
   }
}

// Modifies the input data, and replaces C/C++ comments with whitespace
// when the line contains other data too. If the line contains only
// a comment or white data, remember this in the flag bContainsPureComment.
void SourceData::FileData::removeComments()
{
   int line=0;
   char* p = (char*)m_pBuf;
   bool bWithinComment=false;
   int size = m_size;
   for(int i=0; i<size; ++i )
   {
//      std::cout << "2        " << std::string(&p[i], m_v[line].size) << std::endl;
      bool bWhite = true;
      bool bCommentInLine = false;

      if ( bWithinComment )
      {
         int commentStart = i;
         bCommentInLine = true;

         for( ; !isLineOrBufEnd(p,i,size); ++i)
         {
            if ( i+1<size && p[i]=='*' && p[i+1]=='/')  // end of the comment
            {
               i+=2;

               // More comments in the line?
               checkLineForComments( p, i, size, bWhite, bCommentInLine, bWithinComment );
               if ( !bWhite )
               {
                  memset( &p[commentStart], ' ', i-commentStart );
               }
               break;
            }
         }
      }
      else
      {
         checkLineForComments( p, i, size, bWhite, bCommentInLine, bWithinComment );
      }

      // end of line
      assert( isLineOrBufEnd(p,i,size));
      m_v[line].bContainsPureComment = bCommentInLine && bWhite;
/*      std::cout << line << " : " <<
       ( bCommentInLine ?  "c" : " " ) <<
       ( bWhite ? "w " : "  ") <<
       std::string(pLD[line].pLine, pLD[line].size) << std::endl;*/

      ++line;
   }
}



// First step
void calcDiff3LineListUsingAB(
   const DiffList* pDiffListAB,
   Diff3LineList& d3ll
   )
{
   // First make d3ll for AB (from pDiffListAB)

   DiffList::const_iterator i=pDiffListAB->begin();
   int lineA=0;
   int lineB=0;
   Diff d(0,0,0);

   for(;;)
   {
      if ( d.nofEquals==0 && d.diff1==0 && d.diff2==0 )
      {
         if ( i!=pDiffListAB->end() )
         {
            d=*i;
            ++i;
         }
         else
            break;
      }

      Diff3Line d3l;
      if( d.nofEquals>0 )
      {
         d3l.bAEqB = true;
         d3l.lineA = lineA;
         d3l.lineB = lineB;
         --d.nofEquals;
         ++lineA;
         ++lineB;
      }
      else if ( d.diff1>0 && d.diff2>0 )
      {
         d3l.lineA = lineA;
         d3l.lineB = lineB;
         --d.diff1;
         --d.diff2;
         ++lineA;
         ++lineB;
      }
      else if ( d.diff1>0 )
      {
         d3l.lineA = lineA;
         --d.diff1;
         ++lineA;
      }
      else if ( d.diff2>0 )
      {
         d3l.lineB = lineB;
         --d.diff2;
         ++lineB;
      }

      d3ll.push_back( d3l );
   }
}


// Second step
void calcDiff3LineListUsingAC(
   const DiffList* pDiffListAC,
   Diff3LineList& d3ll
   )
{
   ////////////////
   // Now insert data from C using pDiffListAC

   DiffList::const_iterator i=pDiffListAC->begin();
   Diff3LineList::iterator i3 = d3ll.begin();
   int lineA=0;
   int lineC=0;
   Diff d(0,0,0);

   for(;;)
   {
      if ( d.nofEquals==0 && d.diff1==0 && d.diff2==0 )
      {
         if ( i!=pDiffListAC->end() )
         {
            d=*i;
            ++i;
         }
         else
            break;
      }

      Diff3Line d3l;
      if( d.nofEquals>0 )
      {
         // Find the corresponding lineA
         while( (*i3).lineA!=lineA )

            ++i3;
         (*i3).lineC = lineC;
         (*i3).bAEqC = true;
         (*i3).bBEqC = (*i3).bAEqB;

         --d.nofEquals;
         ++lineA;
         ++lineC;
         ++i3;
      }
      else if ( d.diff1>0 && d.diff2>0 )
      {
         d3l.lineC = lineC;
         d3ll.insert( i3, d3l );
         --d.diff1;
         --d.diff2;
         ++lineA;
         ++lineC;
      }
      else if ( d.diff1>0 )
      {
         --d.diff1;
         ++lineA;
      }
      else if ( d.diff2>0 )

      {
         d3l.lineC = lineC;
         d3ll.insert( i3, d3l );
         --d.diff2;
         ++lineC;
      }
   }
}

// Third step
void calcDiff3LineListUsingBC(
   const DiffList* pDiffListBC,
   Diff3LineList& d3ll
   )
{
   ////////////////
   // Now improve the position of data from C using pDiffListBC
   // If a line from C equals a line from A then it is in the
   // same Diff3Line already.
   // If a line from C equals a line from B but not A, this
   // information will be used here.

   DiffList::const_iterator i=pDiffListBC->begin();
   Diff3LineList::iterator i3b = d3ll.begin();
   Diff3LineList::iterator i3c = d3ll.begin();
   int lineB=0;
   int lineC=0;
   Diff d(0,0,0);

   for(;;)
   {
      if ( d.nofEquals==0 && d.diff1==0 && d.diff2==0 )
      {
         if ( i!=pDiffListBC->end() )
         {
            d=*i;
            ++i;
         }
         else
            break;
      }

      Diff3Line d3l;
      if( d.nofEquals>0 )
      {
         // Find the corresponding lineB and lineC
         while( i3b!=d3ll.end() && (*i3b).lineB!=lineB )
            ++i3b;

         while( i3c!=d3ll.end() && (*i3c).lineC!=lineC )
            ++i3c;

         assert(i3b!=d3ll.end());
         assert(i3c!=d3ll.end());

         if ( i3b==i3c )
         {
            assert( (*i3b).lineC == lineC );
            (*i3b).bBEqC = true;
         }
         else //if ( !(*i3b).bAEqB )
         {
            // Is it possible to move this line up?
            // Test if no other B's are used between i3c and i3b

            // First test which is before: i3c or i3b ?
            Diff3LineList::iterator i3c1 = i3c;

            Diff3LineList::iterator i3b1 = i3b;
            while( i3c1!=i3b  &&  i3b1!=i3c )
            {
               assert(i3b1!=d3ll.end() || i3c1!=d3ll.end());
               if( i3c1!=d3ll.end() ) ++i3c1;
               if( i3b1!=d3ll.end() ) ++i3b1;
            }

            if( i3c1==i3b  &&  !(*i3b).bAEqB ) // i3c before i3b
            {
               Diff3LineList::iterator i3 = i3c;
               int nofDisturbingLines = 0;
               while( i3 != i3b && i3!=d3ll.end() )

               {
                  if ( (*i3).lineB != -1 )
                     ++nofDisturbingLines;
                  ++i3;
               }

               if ( nofDisturbingLines>0 && nofDisturbingLines < d.nofEquals )
               {
                  // Move the disturbing lines up, out of sight.
                  i3 = i3c;

                  while( i3 != i3b )
                  {
                     if ( (*i3).lineB != -1 )
                     {
                        Diff3Line d3l;
                        d3l.lineB = (*i3).lineB;
                        (*i3).lineB = -1;


                        (*i3).bAEqB = false;
                        (*i3).bBEqC = false;
                        d3ll.insert( i3c, d3l );
                     }
                     ++i3;
                  }
                  nofDisturbingLines=0;
               }

               if ( nofDisturbingLines == 0 )
               {
                  // Yes, the line from B can be moved.
                  (*i3b).lineB = -1;   // This might leave an empty line: removed later.
                  (*i3b).bAEqB = false;
                  (*i3b).bAEqC = false;
                  (*i3b).bBEqC = false;
                  //(*i3b).lineC = -1;
                  (*i3c).lineB = lineB;

                  (*i3c).bBEqC = true;

               }

            }

            else if( i3b1==i3c  &&  !(*i3b).bAEqC)
            {
               Diff3LineList::iterator i3 = i3b;
               int nofDisturbingLines = 0;
               while( i3 != i3c && i3!=d3ll.end() )
               {
                  if ( (*i3).lineC != -1 )
                     ++nofDisturbingLines;
                  ++i3;
               }

               if ( nofDisturbingLines>0 && nofDisturbingLines < d.nofEquals )
               {
                  // Move the disturbing lines up, out of sight.
                  i3 = i3b;
                  while( i3 != i3c )
                  {
                     if ( (*i3).lineC != -1 )
                     {
                        Diff3Line d3l;
                        d3l.lineC = (*i3).lineC;
                        (*i3).lineC = -1;
                        (*i3).bAEqC = false;
                        (*i3).bBEqC = false;
                        d3ll.insert( i3b, d3l );
                     }
                     ++i3;

                  }
                  nofDisturbingLines=0;
               }

               if ( nofDisturbingLines == 0 )
               {
                  // Yes, the line from C can be moved.
                  (*i3c).lineC = -1;   // This might leave an empty line: removed later.
                  (*i3c).bAEqC = false;
                  (*i3c).bBEqC = false;
                  //(*i3c).lineB = -1;
                  (*i3b).lineC = lineC;
                  (*i3b).bBEqC = true;
               }
            }
         }

         --d.nofEquals;
         ++lineB;
         ++lineC;
         ++i3b;
         ++i3c;
      }
      else if ( d.diff1>0 )
      {
         Diff3LineList::iterator i3 = i3b;
         while( (*i3).lineB!=lineB )
            ++i3;
         if( i3 != i3b  &&  (*i3).bAEqB==false )
         {
            // Take this line and move it up as far as possible
            d3l.lineB = lineB;
            d3ll.insert( i3b, d3l );
            (*i3).lineB = -1;
         }
         else
         {
            i3b=i3;
         }
         --d.diff1;
         ++lineB;
         ++i3b;


         if( d.diff2>0 )
         {
            --d.diff2;
            ++lineC;
         }
      }
      else if ( d.diff2>0 )
      {
         --d.diff2;
         ++lineC;
      }
   }
/*
   Diff3LineList::iterator it = d3ll.begin();
   int li=0;
   for( ; it!=d3ll.end(); ++it, ++li )
   {
      printf( "%4d %4d %4d %4d  A%c=B A%c=C B%c=C\n",
         li, (*it).lineA, (*it).lineB, (*it).lineC,
         (*it).bAEqB ? '=' : '!', (*it).bAEqC ? '=' : '!', (*it).bBEqC ? '=' : '!' );
   }
   printf("\n");*/
}

#ifdef _WIN32
using ::equal;
#endif

// Fourth step
void calcDiff3LineListTrim(
   Diff3LineList& d3ll, const LineData* pldA, const LineData* pldB, const LineData* pldC
   )
{
   const Diff3Line d3l_empty;
   d3ll.remove( d3l_empty );

   Diff3LineList::iterator i3 = d3ll.begin();
   Diff3LineList::iterator i3A = d3ll.begin();
   Diff3LineList::iterator i3B = d3ll.begin();
   Diff3LineList::iterator i3C = d3ll.begin();

   int line=0;
   int lineA=0;
   int lineB=0;
   int lineC=0;

   // The iterator i3 and the variable line look ahead.
   // The iterators i3A, i3B, i3C and corresponding lineA, lineB and lineC stop at empty lines, if found.
   // If possible, then the texts from the look ahead will be moved back to the empty places.

   for( ; i3!=d3ll.end(); ++i3, ++line )
   {
      if( line>lineA && (*i3).lineA != -1 && (*i3A).lineB!=-1 && (*i3A).bBEqC  &&
          ::equal( pldA[(*i3).lineA], pldB[(*i3A).lineB], false ))
      {
         // Empty space for A. A matches B and C in the empty line. Move it up.
         (*i3A).lineA = (*i3).lineA;
         (*i3A).bAEqB = true;
         (*i3A).bAEqC = true;
         (*i3).lineA = -1;
         (*i3).bAEqB = false;
         (*i3).bAEqC = false;
         ++i3A;
         ++lineA;
      }

      if( line>lineB && (*i3).lineB != -1 && (*i3B).lineA!=-1 && (*i3B).bAEqC  &&
          ::equal( pldB[(*i3).lineB], pldA[(*i3B).lineA], false ))
      {
         // Empty space for B. B matches A and C in the empty line. Move it up.
         (*i3B).lineB = (*i3).lineB;
         (*i3B).bAEqB = true;
         (*i3B).bBEqC = true;
         (*i3).lineB = -1;
         (*i3).bAEqB = false;
         (*i3).bBEqC = false;
         ++i3B;
         ++lineB;
      }

      if( line>lineC && (*i3).lineC != -1 && (*i3C).lineA!=-1 && (*i3C).bAEqB  &&
          ::equal( pldC[(*i3).lineC], pldA[(*i3C).lineA], false ))
      {
         // Empty space for C. C matches A and B in the empty line. Move it up.
         (*i3C).lineC = (*i3).lineC;
         (*i3C).bAEqC = true;
         (*i3C).bBEqC = true;
         (*i3).lineC = -1;
         (*i3).bAEqC = false;
         (*i3).bBEqC = false;
         ++i3C;
         ++lineC;
      }

      if( line>lineA && (*i3).lineA != -1 && !(*i3).bAEqB && !(*i3).bAEqC )
      {
         // Empty space for A. A doesn't match B or C. Move it up.
         (*i3A).lineA = (*i3).lineA;
         (*i3).lineA = -1;
         ++i3A;
         ++lineA;
      }

      if( line>lineB && (*i3).lineB != -1 && !(*i3).bAEqB && !(*i3).bBEqC )
      {
         // Empty space for B. B matches neither A nor C. Move B up.
         (*i3B).lineB = (*i3).lineB;
         (*i3).lineB = -1;
         ++i3B;
         ++lineB;
      }

      if( line>lineC && (*i3).lineC != -1 && !(*i3).bAEqC && !(*i3).bBEqC )
      {
         // Empty space for C. C matches neither A nor B. Move C up.
         (*i3C).lineC = (*i3).lineC;
         (*i3).lineC = -1;
         ++i3C;
         ++lineC;
      }

      if( line>lineA && line>lineB && (*i3).lineA != -1 && (*i3).bAEqB && !(*i3).bAEqC )
      {
         // Empty space for A and B. A matches B, but not C. Move A & B up.
         Diff3LineList::iterator i = lineA > lineB ? i3A   : i3B;
         int                     l = lineA > lineB ? lineA : lineB;

         (*i).lineA = (*i3).lineA;
         (*i).lineB = (*i3).lineB;
         (*i).bAEqB = true;

         (*i3).lineA = -1;
         (*i3).lineB = -1;
         (*i3).bAEqB = false;
         i3A = i;
         i3B = i;
         ++i3A;
         ++i3B;
         lineA=l+1;
         lineB=l+1;
      }
      else if( line>lineA && line>lineC && (*i3).lineA != -1 && (*i3).bAEqC && !(*i3).bAEqB )
      {
         // Empty space for A and C. A matches C, but not B. Move A & C up.
         Diff3LineList::iterator i = lineA > lineC ? i3A   : i3C;
         int                     l = lineA > lineC ? lineA : lineC;
         (*i).lineA = (*i3).lineA;
         (*i).lineC = (*i3).lineC;
         (*i).bAEqC = true;

         (*i3).lineA = -1;
         (*i3).lineC = -1;
         (*i3).bAEqC = false;
         i3A = i;
         i3C = i;
         ++i3A;
         ++i3C;
         lineA=l+1;
         lineC=l+1;
      }
      else if( line>lineB && line>lineC && (*i3).lineB != -1 && (*i3).bBEqC && !(*i3).bAEqC )
      {
         // Empty space for B and C. B matches C, but not A. Move B & C up.
         Diff3LineList::iterator i = lineB > lineC ? i3B   : i3C;
         int                     l = lineB > lineC ? lineB : lineC;
         (*i).lineB = (*i3).lineB;
         (*i).lineC = (*i3).lineC;
         (*i).bBEqC = true;

         (*i3).lineB = -1;
         (*i3).lineC = -1;
         (*i3).bBEqC = false;
         i3B = i;
         i3C = i;
         ++i3B;
         ++i3C;
         lineB=l+1;
         lineC=l+1;
      }

      if ( (*i3).lineA != -1 )
      {
         lineA = line+1;
         i3A = i3;
         ++i3A;
      }
      if ( (*i3).lineB != -1 )
      {
         lineB = line+1;
         i3B = i3;
         ++i3B;
      }
      if ( (*i3).lineC != -1 )
      {
         lineC = line+1;
         i3C = i3;
         ++i3C;
      }
   }

   d3ll.remove( d3l_empty );

/*

   Diff3LineList::iterator it = d3ll.begin();
   int li=0;
   for( ; it!=d3ll.end(); ++it, ++li )
   {
      printf( "%4d %4d %4d %4d  A%c=B A%c=C B%c=C\n",
         li, (*it).lineA, (*it).lineB, (*it).lineC,
         (*it).bAEqB ? '=' : '!', (*it).bAEqC ? '=' : '!', (*it).bBEqC ? '=' : '!' );

   }
*/
}

void calcWhiteDiff3Lines(
   Diff3LineList& d3ll, const LineData* pldA, const LineData* pldB, const LineData* pldC
   )
{
   Diff3LineList::iterator i3 = d3ll.begin();

   for( ; i3!=d3ll.end(); ++i3 )
   {
      i3->bWhiteLineA = ( (*i3).lineA == -1  ||  pldA[(*i3).lineA].whiteLine() || pldA[(*i3).lineA].bContainsPureComment );
      i3->bWhiteLineB = ( (*i3).lineB == -1  ||  pldB[(*i3).lineB].whiteLine() || pldB[(*i3).lineB].bContainsPureComment );
      i3->bWhiteLineC = ( (*i3).lineC == -1  ||  pldC[(*i3).lineC].whiteLine() || pldC[(*i3).lineC].bContainsPureComment );
   }
}

// Just make sure that all input lines are in the output too, exactly once.
void debugLineCheck( Diff3LineList& d3ll, int size, int idx )
{
   Diff3LineList::iterator it = d3ll.begin();
   int i=0;

   for ( it = d3ll.begin(); it!= d3ll.end(); ++it )
   {
      int l=0;
      if      (idx==1) l=(*it).lineA;
      else if (idx==2) l=(*it).lineB;
      else if (idx==3) l=(*it).lineC;
      else assert(false);

      if ( l!=-1 )
      {
         if( l!=i )
         {
            KMessageBox::error(0, i18n(
               "Data loss error:\n"
               "If it is reproducable please contact the author.\n"
               ), i18n("Severe Internal Error") );
            assert(false);
            std::cerr << "Severe Internal Error.\n";
            ::exit(-1);
         }
         ++i;
      }
   }

   if( size!=i )
   {
      KMessageBox::error(0, i18n(
         "Data loss error:\n"
         "If it is reproducable please contact the author.\n"
         ), i18n("Severe Internal Error") );
      assert(false);
      std::cerr << "Severe Internal Error.\n";
      ::exit(-1);
   }
}

inline bool equal( char c1, char c2, bool /*bStrict*/ )
{
   // If bStrict then white space doesn't match

   //if ( bStrict &&  ( c1==' ' || c1=='\t' ) )
   //   return false;

   return c1==c2;
}


// My own diff-invention:
template <class T>
void calcDiff( const T* p1, int size1, const T* p2, int size2, DiffList& diffList, int match, int maxSearchRange )
{
   diffList.clear();

   const T* p1start = p1;
   const T* p2start = p2;
   const T* p1end=p1+size1;
   const T* p2end=p2+size2;
   for(;;)
   {
      int nofEquals = 0;
      while( p1!=p1end &&  p2!=p2end && equal(*p1, *p2, false) )
      {
         ++p1;
         ++p2;
         ++nofEquals;
      }

      bool bBestValid=false;
      int bestI1=0;
      int bestI2=0;
      int i1=0;
      int i2=0;
      for( i1=0; ; ++i1 )
      {
         if ( &p1[i1]==p1end || ( bBestValid && i1>= bestI1+bestI2))
         {
            break;
         }
         for(i2=0;i2<maxSearchRange;++i2)
         {
            if( &p2[i2]==p2end ||  ( bBestValid && i1+i2>=bestI1+bestI2) )
            {
               break;
            }
            else if(  equal( p2[i2], p1[i1], true ) &&
                      ( match==1 ||  abs(i1-i2)<3  || ( &p2[i2+1]==p2end  &&  &p1[i1+1]==p1end ) ||
                         ( &p2[i2+1]!=p2end  &&  &p1[i1+1]!=p1end  && equal( p2[i2+1], p1[i1+1], false ))
                      )
                   )
            {
               if ( i1+i2 < bestI1+bestI2 || bBestValid==false )
               {
                  bestI1 = i1;
                  bestI2 = i2;
                  bBestValid = true;
                  break;
               }
            }
         }
      }

      // The match was found using the strict search. Go back if there are non-strict
      // matches.
      while( bestI1>=1 && bestI2>=1 && equal( p1[bestI1-1], p2[bestI2-1], false ) )
      {
         --bestI1;
         --bestI2;
      }


      bool bEndReached = false;
      if (bBestValid)
      {
         // continue somehow
         Diff d(nofEquals, bestI1, bestI2);
         diffList.push_back( d );

         p1 += bestI1;
         p2 += bestI2;
      }
      else
      {
         // Nothing else to match.
         Diff d(nofEquals, p1end-p1, p2end-p2);
         diffList.push_back( d );

         bEndReached = true; //break;
      }

      // Sometimes the algorithm that chooses the first match unfortunately chooses
      // a match where later actually equal parts don't match anymore.
      // A different match could be achieved, if we start at the end.
      // Do it, if it would be a better match.
      int nofUnmatched = 0;
      const T* pu1 = p1-1;
      const T* pu2 = p2-1;
      while ( pu1>=p1start && pu2>=p2start && equal( *pu1, *pu2, false ) )
      {
         ++nofUnmatched;
         --pu1;
         --pu2;
      }

      Diff d = diffList.back();
      if ( nofUnmatched > 0 )
      {
         // We want to go backwards the nofUnmatched elements and redo
         // the matching
         d = diffList.back();
         Diff origBack = d;
         diffList.pop_back();

         while (  nofUnmatched > 0 )
         {
            if ( d.diff1 > 0  &&  d.diff2 > 0 )
            {
               --d.diff1;
               --d.diff2;
               --nofUnmatched;
            }
            else if ( d.nofEquals > 0 )
            {
               --d.nofEquals;
               --nofUnmatched;
            }

            if ( d.nofEquals==0 && (d.diff1==0 || d.diff2==0) &&  nofUnmatched>0 )
            {
               if ( diffList.empty() )
                  break;
               d.nofEquals += diffList.back().nofEquals;
               d.diff1 += diffList.back().diff1;
               d.diff2 += diffList.back().diff2;
               diffList.pop_back();
               bEndReached = false;
            }
         }

         if ( bEndReached )
            diffList.push_back( origBack );
         else
         {

            p1 = pu1 + 1 + nofUnmatched;
            p2 = pu2 + 1 + nofUnmatched;
            diffList.push_back( d );
         }
      }
      if ( bEndReached )
         break;
   }

#ifndef NDEBUG
   // Verify difflist
   {
      int l1=0;
      int l2=0;
      DiffList::iterator i;
      for( i = diffList.begin(); i!=diffList.end(); ++i )
      {
         l1+= i->nofEquals + i->diff1;
         l2+= i->nofEquals + i->diff2;
      }

      //if( l1!=p1-p1start || l2!=p2-p2start )
      if( l1!=size1 || l2!=size2 )
         assert( false );
   }
#endif
}

void fineDiff(
   Diff3LineList& diff3LineList,
   int selector,
   const LineData* v1,
   const LineData* v2,
   bool& bTextsTotalEqual
   )
{
   // Finetuning: Diff each line with deltas
   int maxSearchLength=500;
   Diff3LineList::iterator i;
   int k1=0;
   int k2=0;
   bTextsTotalEqual = true;
   int listSize = diff3LineList.size();
   int listIdx = 0;
   for( i= diff3LineList.begin(); i!= diff3LineList.end(); ++i)
   {
      if      (selector==1){ k1=i->lineA; k2=i->lineB; }
      else if (selector==2){ k1=i->lineB; k2=i->lineC; }
      else if (selector==3){ k1=i->lineC; k2=i->lineA; }
      else assert(false);
      if( k1==-1 && k2!=-1  ||  k1!=-1 && k2==-1 ) bTextsTotalEqual=false;
      if( k1!=-1 && k2!=-1 )
      {
         if ( v1[k1].size != v2[k2].size || memcmp( v1[k1].pLine, v2[k2].pLine, v1[k1].size)!=0 )
         {
            bTextsTotalEqual = false;
            DiffList* pDiffList = new DiffList;
//            std::cout << std::string( v1[k1].pLine, v1[k1].size ) << "\n";
            calcDiff( v1[k1].pLine, v1[k1].size, v2[k2].pLine, v2[k2].size, *pDiffList, 2, maxSearchLength );

            // Optimize the diff list.
            DiffList::iterator dli;
            bool bUsefulFineDiff = false;
            for( dli = pDiffList->begin(); dli!=pDiffList->end(); ++dli)
            {
               if( dli->nofEquals >= 4 )
               {
                  bUsefulFineDiff = true;
                  break;
               }
            }

            for( dli = pDiffList->begin(); dli!=pDiffList->end(); ++dli)
            {
               if( dli->nofEquals < 4  &&  (dli->diff1>0 || dli->diff2>0) 
                  && !( bUsefulFineDiff && dli==pDiffList->begin() )
               )
               {
                  dli->diff1 += dli->nofEquals;
                  dli->diff2 += dli->nofEquals;
                  dli->nofEquals = 0;
               }
            }

            if      (selector==1){ delete (*i).pFineAB; (*i).pFineAB = pDiffList; }
            else if (selector==2){ delete (*i).pFineBC; (*i).pFineBC = pDiffList; }
            else if (selector==3){ delete (*i).pFineCA; (*i).pFineCA = pDiffList; }
            else assert(false);
         }

         if ( (v1[k1].bContainsPureComment || v1[k1].whiteLine()) && (v2[k2].bContainsPureComment || v2[k2].whiteLine()))
         {
            if      (selector==1){ i->bAEqB = true; }
            else if (selector==2){ i->bBEqC = true; }
            else if (selector==3){ i->bAEqC = true; }
            else assert(false);
         }
      }
      ++listIdx;
      g_pProgressDialog->setSubCurrent(double(listIdx)/listSize);
   }
}


// Convert the list to a vector of pointers
void calcDiff3LineVector( const Diff3LineList& d3ll, Diff3LineVector& d3lv )
{
   d3lv.resize( d3ll.size() );
   Diff3LineList::const_iterator i;
   int j=0;
   for( i= d3ll.begin(); i!= d3ll.end(); ++i, ++j)
   {
      d3lv[j] = &(*i);
   }
   assert( j==(int)d3lv.size() );
}


#include "diff.moc"
