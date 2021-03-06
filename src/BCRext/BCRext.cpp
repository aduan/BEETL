/**
 ** Copyright (c) 2011 Illumina, Inc.
 **
 ** 
 ** This software is covered by the "Illumina Non-Commercial Use Software
 ** and Source Code License Agreement" and any user of this software or
 ** source file is bound by the terms therein (see accompanying file
 ** Illumina_Non-Commercial_Use_Software_and_Source_Code_License_Agreement.pdf)
 **
 ** This file is part of the BEETL software package.
 **
 ** Citation: Markus J. Bauer, Anthony J. Cox and Giovanna Rosone
 ** Lightweight BWT Construction for Very Large String Collections. 
 ** Proceedings of CPM 2011, pp.219-231
 **
 **/


#include <iostream>
#include <fstream>
#include <string>
#include <cassert>
#include <cstring>
#include <vector>
#include <cstdlib>
#include <string.h>
#include "Types.hh"
#include "Config.hh"
#include "Timer.hh"
#include "Tools.h"

#include "LetterCount.hh"
#include "ReadBuffer.hh"
#include "BwtWriter.hh"
#include "BwtReader.hh"

#include "BCRext.hh"


#ifdef USE_POSIX_FILE_OPTIMIZATIONS
#define _XOPEN_SOURCE 600
#endif
#include <fcntl.h>


#define BCREXT_ID "@(#) $Id: BCRext.cpp,v 1.8 2011/12/20 14:33:24 tjakobi Exp $"


using namespace std;

//const int maxSeqSize(1023);
//const int bwtBufferSize(16384); // 1<<20=1048576
//typedef unsigned int uint;

// below limits to 4 billion reads max - change to unsigned long for more
//typedef unsigned int SequenceNumberType;

// Should work for BWT of up to 2^64 characters in size
//typedef unsigned long LetterCountType;




//typedef ReadBufferASCII ReadBuffer;
#ifdef USE_4_BITS_PER_BASE
typedef ReadBuffer4Bits ReadBuffer;
#else
#ifdef USE_PREFIX_ONLY
typedef ReadBuffer4Bits ReadBuffer;
#else
typedef ReadBufferASCII ReadBuffer;
#endif
#endif


#ifdef COMPRESS_BWT
typedef BwtReaderRunLength BwtReader;
typedef BwtWriterRunLength BwtWriter;
#else
typedef BwtReaderASCII BwtReader;
typedef BwtWriterASCII BwtWriter;
#endif


const string tmp1("temp1-XX");
const string tmp2("temp2-XX");
//const int tmp1Size(strlen(tmp1));
//const int tmp2Size(strlen(tmp2));


// added by Tobias, interface to new Beetl executable
BCRext::BCRext(bool huffman, bool runlength,
	       bool ascii, bool implicitSort,
	       bool seqFile, string inFile, string prefix) :

    // set tool flags
  useHuffmanEncoder_(huffman),
  useRunlengthEncoder_(runlength),
  useAsciiEncoder_(ascii),
  useImplicitSort_(implicitSort),
  useSeqFile_ (seqFile)
{

    // get memory allocated
    prefix_ = new char[prefix.length()+1];
    inFile_ = new char[inFile.length()+1];

    // copt to char * in order to get valid c strings
    inFile.copy(inFile_, inFile.length());
    prefix.copy(prefix_, prefix.length());

    // append \0 to obtain a valid escaped c string
    inFile_[inFile.length()] = '\0';
    prefix_[prefix.length()] = '\0';
    
}

// called from beetl class, replaces main method
// args have been set through constructor before
void BCRext::run(void) {

  Timer  timer;

  //string prefix = (string)"[" + (string)args[0] + (string)"]: ";
  
  string prefix = (string)"[" + (string)"BCRext" + (string)"]: ";
  cerr << prefix << "time now is " << timer.timeNow();
  // Tony 13.6.12 - BCREXT_ID is not informative now we are in git world
  //  cerr << prefix << "software version is " << BCREXT_ID << endl;


  const string fileStem(prefix_);
  const string fileStemTemp("tmp");


  string tmpIn=fileStem;
  string tmpOut=fileStemTemp;
  string tmpSwap;
  string fileName;

  // output streams for sequences - 1 per pile
  vector <FILE*> outSeq(alphabetSize);
  // output streams for positions of suffixes in pile - 1 per pile
  vector <FILE*> outPtr(alphabetSize);
  // output stream for updated BWTs - 1 per pile
  vector <BwtWriterBase*> outBwt(alphabetSize);
  // output streams for original read numberings - 1 per pile
  vector <FILE*> outNum(alphabetSize);

  // input streams for BWT from previous iter - 1 per pile
  // this one used to compute the counts to do backward search
  vector <BwtReaderBase*> inBwt(alphabetSize);

  // input streams for BWT from previous iter - 1 per pile
  // this one used to read the BWT chunk by chunk to allow new
  // chars to be interspersed
  vector <BwtReaderBase*> inBwt2(alphabetSize);

  FILE* inSeq;
  //  FILE* inPtr;
  //  FILE* inNum;
  FILE* outDollarBwt; 

  //  string thisSeq;
  char seqBuf[maxSeqSize+1];
  char descBuf[maxSeqSize+1];
  //  char* seqBuff;

  vector<char> bwtBuf;
  // extra byte accounts for a fact that inserted BWT characters
  // are appended to the buffer after the interspersed characters so as to
  // make a single write to file
  bwtBuf.resize(bwtBufferSize+1); 

    if (    whichPile[(int) '$'] != 0 ||
            whichPile[(int) 'A'] != 1 ||
            whichPile[(int) 'C'] != 2 ||
            whichPile[(int) 'G'] != 3 ||
            whichPile[(int) alphabet[4]] != 4 ||
            whichPile[(int) alphabet[5]] != 5 ||
            whichPile[(int) notInAlphabet] != 6
          ){
      cerr << "Something seems to be wrong with the alphabet table!" << endl;
      exit (-1);
  }


  cerr << prefix << "Using alphabet = " << alphabet << ", size = " << alphabetSize
       << endl;
  //  cerr << BUFSIZ << endl

    if (alphabetSize >= 10) {
        cerr << "Alphabet sizes larger than 9 are not supported yet." << endl;
        exit(-1);
    }

    if (sizeof (LetterCountType) != 8) {
        cerr << "Long long size is not 8 Byte. Aborting." << endl;
        exit(-1);
    }


  SequenceNumberType seqNum(0); 


  const LetterCountType sameAsPrevFlag(((LetterCountType)1)<<((8*sizeof(LetterCountType))-1));
  const LetterCountType sameAsPrevMask(~sameAsPrevFlag);

  //  cout << sameAsPrevFlag << " "<< sameAsPrevMask << " "<< (sameAsPrevMask&sameAsPrevFlag) << " " << (((LetterCountType)1)<<63) << endl;

  LetterCountType seqPtr;
  int thisPile, lastPile;
  LetterCountType posInPile;
  //  char inChar;
  long long charsToGrab;// charsLeft, charsThisBatch; // TBD make these long?
  //uint charsToGrab;// charsLeft, charsThisBatch; // TBD make these long?


  if ((strlen(inFile_)==0))
  {
    cerr << prefix << "Will read sequences from standard input" << endl;
    inSeq=stdin;
  } // ~if
  else
  {
    cerr << prefix 
	 << "Will read sequences from file " << inFile_ << endl;
    inSeq=fopen(inFile_,"r");
  } // ~else

  // read first sequence to determine read size

	if (!useSeqFile_) {
		// read the first line with fasta header and the sequence afterwards
		fgets(descBuf, maxSeqSize, inSeq); // may be ugly, but probably 
		fgets(seqBuf, maxSeqSize, inSeq); // faster than formated read stuff

	} else {

		// just read the first line
		fgets( seqBuf, maxSeqSize, inSeq);
	}

  const int seqSize(strlen(seqBuf)-1); // -1 compensates for \n at end
  cerr << prefix << "Assuming all sequences are of length " << seqSize << endl;
  //  inFile.seekg(0,ios::beg);
  //  rewind(inSeq);

  if ((seqSize%2)==1) // if odd
  {
    //    cout << "ODD" << endl;
    tmpIn=fileStem;
    tmpOut=fileStemTemp;
  } // ~if
  else
  {
    //    cout << "EVEN" << endl;
    tmpIn=fileStemTemp;
    tmpOut=fileStem;
  } // ~else

  getFileName(fileStem,'B',0,fileName);
  readWriteCheck(fileName.c_str(),true);
  outDollarBwt=fopen(fileName.c_str(),"w");


  for (int j(1);j<alphabetSize;j++)
  {

    getFileName(tmpIn,'S',j,fileName);
    readWriteCheck(fileName.c_str(),true);
    outSeq[j]= fopen(fileName.c_str(), "w");
    
    
    getFileName(tmpIn,'P',j,fileName);
    readWriteCheck(fileName.c_str(),true);        
    outPtr[j]= fopen(fileName.c_str(), "w");


#ifdef TRACK_SEQUENCE_NUMBER
    getFileName(tmpIn,'N',j,fileName);
    readWriteCheck(fileName.c_str(),true);
    outNum[j]= fopen(fileName.c_str(), "w");
#endif

    getFileName(tmpIn,'B',j,fileName);

    if (useImplicitSort_||useAsciiEncoder_)
      outBwt[j] = new BwtWriterASCII( fileName.c_str() );
    else if (useHuffmanEncoder_)
      outBwt[j] = new BwtWriterHuffman( fileName.c_str() );
    else if (useRunlengthEncoder_)
      outBwt[j] = new BwtWriterRunLength( fileName.c_str() );


  } // ~for

  LetterCount dollars;
  //  vector<LetterCount> alreadyInPile(alphabetSize);
  //  vector<LetterCount> countedThisIter(alphabetSize);
  LetterCountEachPile alreadyInPile;
  LetterCountEachPile countedThisIter;
  LetterCountEachPile newCharsThisIter;

  // TBD Rationalize count names wrt first and subsequent iterations
  LetterCount addedSoFar;
  LetterCount outputSoFar;

  LetterCount prevCharsOutputThisIter;
  LetterCount newCharsAddedThisIter;
  

  //  LetterCount readSoFar[alphabetSize];

  // First iteration 
  // - move original sequence into piles based on last character
  // - work out BWT corresponding to 0-suffixes and 1-suffixes
  // TBD check for invalid chars, do qual masking

  ReadBuffer readBuffer(seqSize,-1,-1,-1);

    if (readBuffer.blockSize_<=seqSize+1) {
        cerr << "ReadBuffer blocksize is too small (" << readBuffer.blockSize_ << "). Aborting." << endl;
        exit(-1);
    }

  // copy first sequence over
  strcpy( readBuffer.seqBufBase_, seqBuf);
	 //  while ( fgets( readBuffer.seqBufBase_, readBuffer.blockSize_, inSeq)!=NULL)
  do
{
	// check if we are reading a fasta file
	if (!useSeqFile_) {
		// read the fasta header
		fgets(descBuf, maxSeqSize, inSeq);
	}

	thisPile=whichPile[(int)readBuffer.seqBufBase_[seqSize-1]];

        // zero is terminator so should not be present
        if (thisPile < 0) {
            cerr << "Pile must not be < 0. Aborting." << endl;
            exit(-1);
        }

        if (thisPile > alphabetSize) {
            cerr << "Pile must not be > alphabet size. Aborting." << endl;
            exit(-1);
        }
    
#ifdef DEBUG
    cout << readBuffer.seqBufBase_ << endl;
#endif

    // count characters and output first N chars of BWT 
    // (those preceding terminator characters)
    dollars.count_[thisPile]++; 

        if (fwrite(readBuffer.seqBufBase_ + seqSize - 1, sizeof (char), 1, outDollarBwt) != 1) {
            cerr << "Could not write to Dollar Pile. Aborting." << endl;
            exit(-1);
        }
    
    //    fprintf( outSeq[thisPile], "%s", readBuffer.seqBufBase_);
    readBuffer.convertFromASCII();
    readBuffer.sendTo( outSeq[thisPile] );

#ifdef TRACK_SEQUENCE_NUMBER
    assert(fwrite( &seqNum, sizeof(SequenceNumberType), 
		   1, outNum[thisPile] )==1);
    //    seqNum++;
#endif

    // create BWT corresponding to 1-suffixes

    if (whichPile[(int)readBuffer.seqBufBase_[seqSize-2]]<0 ||
	whichPile[(int)readBuffer.seqBufBase_[seqSize-2]]>alphabetSize  ) 
    {
      cerr << "Trying to write non alphabet character to pile. Aborting." << endl;
      exit(-1);
    }
    
    countedThisIter[thisPile].count_[whichPile[(int)readBuffer.seqBufBase_[seqSize-2]]]++;   
    //    assert(fwrite( readBuffer.seqBufBase_+seqSize-2, sizeof(char), 1, outBwt[thisPile] )==1);

    seqPtr=*(addedSoFar.count_+thisPile);

    if (useImplicitSort_&&(addedSoFar.count_[thisPile]!=0))
    {
      //      cout << thisPile << " " << addedSoFar.count_[thisPile] << " 1\n";
      seqPtr|=sameAsPrevFlag; // TBD replace if clause with sum
      //      *(readBuffer.seqBufBase_+seqSize-2)+=32;//tolower(*(readBuffer.seqBufBase_+seqSize-2));
      *(readBuffer.seqBufBase_+seqSize-2)=tolower(*(readBuffer.seqBufBase_+seqSize-2));
    }

    (*outBwt[thisPile])( readBuffer.seqBufBase_+seqSize-2, 1 );


    if (fwrite( &seqPtr, sizeof(LetterCountType), 
		1, outPtr[thisPile] )!=1) 
    {
      cerr << "Could not write to pointer pile. Aborting." << endl;
      exit(-1);
    }
    addedSoFar.count_[thisPile]++;
    seqNum++;
  } // ~while
  while ( fgets( readBuffer.seqBufBase_, readBuffer.blockSize_, inSeq)!=NULL);  

  fclose (inSeq);
  fclose (outDollarBwt);

  cerr << prefix << "Read " << seqNum << " sequences" << endl;
  for (int i(1);i<alphabetSize;i++)
  {
    fclose(outSeq[i]);
    fclose(outPtr[i]);
#ifdef TRACK_SEQUENCE_NUMBER
    fclose(outNum[i]);
#endif
    delete outBwt[i];
    //    fclose(outBwt[i]);
  }
  //    return (0);

  LetterCount lastSAPInterval;
  LetterCountType thisSAPInterval;
  bool thisSAPValue;

  //  ReadBuffer buffer(seqSize);

  // Main loop
  for (int i(2);i<=seqSize;i++)
  {
    thisSAPInterval=0;
    lastSAPInterval.clear();

    cout << "Starting iteration " << i << ", time now: " << timer.timeNow();
    cout << "Starting iteration " << i << ", usage: " << timer << endl;

    // don't do j=0 - this is the $ sign which is done already
    for (int j(1); j < alphabetSize; j++) 
    {
      // prep the output files
      
      getFileName(tmpOut, 'S', j, fileName);
      readWriteCheck(fileName.c_str(),true);
      outSeq[j] = fopen(fileName.c_str(), "w");
      
      getFileName(tmpOut, 'P', j, fileName);
      readWriteCheck(fileName.c_str(),true);            
      outPtr[j] = fopen(fileName.c_str(), "w");

#ifdef TRACK_SEQUENCE_NUMBER
      getFileName(tmpOut,'N',j,fileName);
      readWriteCheck(fileName.c_str(),true);
      outNum[j]= fopen(fileName.c_str(), "w");
#endif

      getFileName(tmpOut,'B',j,fileName);

    


      if ((useImplicitSort_&&(i!=seqSize))||useAsciiEncoder_)
	outBwt[j] = new BwtWriterASCII( fileName.c_str() );
      else if (useHuffmanEncoder_)
	outBwt[j] = new BwtWriterHuffman( fileName.c_str() );
      else if (useRunlengthEncoder_)
	outBwt[j] = new BwtWriterRunLength( fileName.c_str() );

      if(useImplicitSort_&&(i==seqSize))
      {
	BwtWriterBase* p(new BwtWriterImplicit(outBwt[j]));
	outBwt[j]=p; // ... and the deception is complete!!!
      } // ~if


#ifdef DEBUG
      cout << "Prepping output file " << tmpOut << endl;
#endif

      setvbuf( outSeq[j], NULL, _IOFBF, 262144);
      //  setvbuf( outPtr[j], NULL, _IOFBF, 65536);
      //  setvbuf( outNum[j], NULL, _IOFBF, 65536);
      //  setvbuf( outBwt[j], NULL, _IOFBF, 65536);

      // prep the input files
      getFileName(tmpIn,'B',j,fileName);
      // select the proper input module
       if (useImplicitSort_||useAsciiEncoder_) 
       {
	 inBwt[j] = new BwtReaderASCII(fileName.c_str());
	 inBwt2[j] = new BwtReaderASCII(fileName.c_str());
       }
       else if (useHuffmanEncoder_) 
       {
	 inBwt[j] = new BwtReaderHuffman(fileName.c_str());
	 inBwt2[j] = new BwtReaderHuffman(fileName.c_str());
       } 
       else if (useRunlengthEncoder_) 
       {
            inBwt[j] = new BwtReaderRunLength(fileName.c_str());
            inBwt2[j] = new BwtReaderRunLength(fileName.c_str());
       }


#ifdef DEBUG
      cout << "Prepping input file " << tmpIn << endl;
#endif

    } // ~for j

    addedSoFar.clear();
    outputSoFar.clear();

    prevCharsOutputThisIter.clear();
    newCharsAddedThisIter.clear();

#ifdef DEBUG
    cout << "already in pile" << endl;
    alreadyInPile.print();
    cout << "counted this iter" << endl;
    countedThisIter.print();
#endif
    countedThisIter.clear();
    newCharsThisIter.clear();

#ifdef DEBUG
    cout << "Count in dollars pile: ";
    dollars.print();
#endif

    int fdSeq, fdNum, fdPtr;

#ifndef TRACK_SEQUENCE_NUMBER
      fdNum=0;
#endif
    
    // don't do j=0; $ sign done already
    for (int j(1);j<alphabetSize;j++)
    { // read each input file in turn

      getFileName(tmpIn,'S',j,fileName);
      readWriteCheck(fileName.c_str(),false);
      fdSeq=open(fileName.c_str(),O_RDONLY,0);

      getFileName(tmpIn,'P',j,fileName);
      readWriteCheck(fileName.c_str(),false);  
      fdPtr=open(fileName.c_str(),O_RDONLY,0);
      

#ifdef TRACK_SEQUENCE_NUMBER
      getFileName(tmpIn,'N',j,fileName);
      readWriteCheck(fileName.c_str(),false);
      fdNum=open(fileName.c_str(),O_RDONLY,0);

      #ifdef USE_POSIX_FILE_OPTIMIZATIONS
	     assert(posix_fadvise(fdNum,0,0,POSIX_FADV_SEQUENTIAL|POSIX_FADV_NOREUSE|POSIX_FADV_WILLNEED)!=-1);
      #endif
#endif

#ifdef USE_POSIX_FILE_OPTIMIZATIONS
      assert(posix_fadvise(fdSeq,0,0,POSIX_FADV_SEQUENTIAL|POSIX_FADV_NOREUSE|POSIX_FADV_WILLNEED)!=-1);
      assert(posix_fadvise(fdPtr,0,0,POSIX_FADV_SEQUENTIAL|POSIX_FADV_NOREUSE|POSIX_FADV_WILLNEED)!=-1);
#endif

#ifdef USE_PREFIX_ONLY
      ReadBufferPrefix buffer(seqSize, i, fdSeq, fdNum, fdPtr);
#else
      ReadBuffer buffer(seqSize, fdSeq, fdNum, fdPtr);
#endif

      while ( buffer.getNext( seqNum, seqPtr))
      {
	if ((seqPtr&sameAsPrevFlag)==0)
	{
	  thisSAPInterval++;
	  thisSAPValue=false;
	} // ~if
	else
	{
	  seqPtr&=sameAsPrevMask;
	  thisSAPValue=true;
	} // ~else

        thisPile = buffer[seqSize - i];

        //thisPile=whichPile[seqBuff[seqSize-i]];

        if (thisPile < 0) {
            cerr << "Pile must not be < 0. Aborting." << endl;
            exit(-1);
        }
        lastPile = buffer[seqSize - i + 1];

          //lastPile=whichPile[seqBuff[seqSize-i+1]];
        if (lastPile < 0) {
            cerr << "Pile must not be < 0. Aborting." << endl;
            exit(-1);
        }



#ifdef DEBUG
	cout << ((thisSAPValue)?'1':'0') << " " << thisSAPInterval << " " << seqPtr << " " << seqNum << " " << thisPile << " " << lastPile << endl;	cout << "Read in " << seqPtr << " " << seqNum << " " << thisPile << " " << lastPile << endl;
	for (int ZZ(0);ZZ<seqSize;ZZ++)
	  { cout << "ZZ " <<ZZ <<endl;  cout << alphabet[buffer[ZZ]] <<endl;}
	cout << endl;

#endif

	// *** work out position in new pile ***

	// sum contents of lexicographically smaller piles
	// ... probably possible to speed this up by storing cumulative sums
#ifdef DEBUG
	cout << "already in pile" << endl;
	alreadyInPile.print();
#endif

	//	posInPile=0;
	posInPile=dollars.count_[thisPile];
	//   cout << posInPile << " " << thisPile << " " << lastPile << endl;
	for (int k(1);k<lastPile; k++)
	{
	  posInPile+=alreadyInPile[k].count_[thisPile]; 
	  //	  cout << posInPile << endl;
	} // ~for k

#ifdef DEBUG
	cout << "posInPile starts at " << posInPile << endl;
	cout << "counting in pile " << alphabet[lastPile] << endl;
#endif

	// count all chars prior to seqPtr in lastPile
	// read seqPtr too, but don't add to countedThisIter
	charsToGrab=seqPtr-addedSoFar.count_[lastPile];//+1;
#ifdef DEBUG
	cout << "charsToGrab " << charsToGrab << endl;
#endif

        // Should now always read at least 1 byte
        if (charsToGrab < 0) {
            cerr << "Tried to grap < 0 chars. Aborting." << endl;
                    exit(-1);
        }

        uint readCountChars = inBwt[lastPile]->readAndCount
                (countedThisIter[lastPile], charsToGrab);

        if (readCountChars != charsToGrab) {
            cerr << "BWT readAndCount returned only "<< readCountChars
                    << " chars. Expected " << charsToGrab
                    << " chars. Aborting." << endl;
                    exit(-1);
        }
                
	inBwt[lastPile]->readAndCount(newCharsThisIter[lastPile],1);


	addedSoFar.count_[lastPile]=seqPtr+1;

	
	posInPile+=countedThisIter[lastPile].count_[thisPile];



#ifdef DEBUG
	cout << "counted this iter" << endl;
	countedThisIter.print();
#endif

	// *** add char into new pile ***

	// read and output bytes up to insertion point

	charsToGrab=posInPile-prevCharsOutputThisIter.count_[thisPile];

        uint readSendChars = inBwt2[thisPile]->readAndSend
                            ( *outBwt[thisPile], charsToGrab );

        if (readSendChars != charsToGrab) {
            cerr << "BWT readAndSend returned only "<< readSendChars
                    << " chars. Expected " << charsToGrab
                    << " chars. Aborting." << endl;
                    exit(-1);
        }

	//	bwtBuf[0]=(seqSize-i-1>=0)?baseNames[buffer[seqSize-i-1]]:'$';
	bwtBuf[0]=(seqSize-i-1>=0)?alphabet[buffer[seqSize-i-1]]:alphabet[0];
	//	if (thisSAPValue==true) bwtBuf[0]+=32;//=tolower(bwtBuf[0]);



	prevCharsOutputThisIter.count_[thisPile]=posInPile;

	// pointer into new pile must be offset by number of new entries added
	//	seqPtr+=newCharsAddedThisIter.count_[thisPile];
	seqPtr=posInPile+newCharsAddedThisIter.count_[thisPile];

	if (useImplicitSort_)
	{
	  if (lastSAPInterval.count_[thisPile]==thisSAPInterval)
	  {
	    bwtBuf[0]=tolower(bwtBuf[0]);
	    //	    bwtBuf[0]+=32;
	    seqPtr|=sameAsPrevFlag;
	    //	  cout << thisSAPInterval << endl;
	  }
	  else
	  {
	    //	  cout << thisSAPInterval << " " << lastSAPInterval.count_[thisPile] << endl;
	    lastSAPInterval.count_[thisPile]=thisSAPInterval;
	  }
	}

	(*outBwt[thisPile])( &bwtBuf[0], 1 );

        if (fwrite(&seqPtr, sizeof (LetterCountType),
                1, outPtr[thisPile]) != 1) {
            cerr << "BWT readAndSend returned only " << readSendChars
                    << " chars. Expected " << charsToGrab
                    << " chars. Aborting." << endl;
                    exit(-1);
        }
                
        
#ifdef DEBUG
	cout << "adding pointer " << seqPtr << " to pile "
	       << alphabet[thisPile] << endl;
#endif
	// then the offset itself is updated
	newCharsAddedThisIter.count_[thisPile]++;

	// do radix sort
	//	fprintf( outSeq[thisPile], "%s\n", seqBuff);
	//	assert(fwrite( seqBuff, sizeof(char), 
	//       1+seqSize, outSeq[thisPile] )==1+seqSize);
	buffer.sendTo( outSeq[thisPile] );


#ifdef TRACK_SEQUENCE_NUMBER
	assert(fwrite( &seqNum, sizeof(SequenceNumberType), 
		       1, outNum[thisPile] )==1);
#endif

      } // ~while


      //      fclose(inSeq);
      close(fdSeq);
      close(fdPtr);
#ifdef TRACK_SEQUENCE_NUMBER
      close(fdNum);
#endif

    } // ~for j

    cout << "All new characters inserted, usage: " << timer << endl;
    for (int j(1);j<alphabetSize;j++)
    {

      while(inBwt[j]->readAndCount
	       (countedThisIter[j],ReadBufferSize)==ReadBufferSize);

    } // ~for j

    cout << "finishing off BWT strings" << endl;
    for (int j(1);j<alphabetSize;j++)
    {

      while(inBwt2[j]->readAndSend( *outBwt[j], ReadBufferSize )
	    ==ReadBufferSize);

    } // ~for

#ifdef DEBUG
    cout << "final value of counted this iter" << endl;
    countedThisIter.print();
    cout << "final value of new chars this iter" << endl;
    newCharsThisIter.print();
#endif

    alreadyInPile+=newCharsThisIter;

    for (int j(1);j<alphabetSize;j++)
    {
      fclose(outSeq[j]);
      fclose(outPtr[j]);
#ifdef TRACK_SEQUENCE_NUMBER
      fclose(outNum[j]);
#endif
      delete (outBwt[j]);
      delete (inBwt[j]);
      delete (inBwt2[j]);
    } // ~for j

    tmpSwap=tmpIn;
    tmpIn=tmpOut;
    tmpOut=tmpSwap;

    cout << "finished iteration " << i  << ", usage: " << timer << endl;

    //    assert(i<2);
  } // ~for i (main iteration)

#ifdef REMOVE_TEMPORARY_FILES
  string fileTypes("SPB");
  for (int j(1);j<alphabetSize;j++)
  {
    for (uint i(0);i<fileTypes.size();i++)
    {
      getFileName(tmpOut,fileTypes[i],j,fileName);
      if (remove(fileName.c_str())!=0)
      {
	cerr << "Warning: failed to clean up temporary file " << fileName
	     << endl;
      } // ~if
      else
      {
	cerr << "Removed temporary file " << fileName
	     << endl;
      } // ~if
    } // ~for i
  } // ~for j
#endif

  cerr << "Final output files are named "<< tmpIn << " and similar" << endl; 
}


