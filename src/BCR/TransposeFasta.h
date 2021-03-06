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

#ifndef TRANPOSEFASTA_INCLUDED
#define TRANPOSEFASTA_INCLUDED


#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <stdio.h>
//#include <stdlib.h>

#include "Tools.h"

#define BUFFERSIZE 1024 // 2^20
//#define CYCLENUM 100

//#define uchar unsigned char


class SeqReaderFile;
using std::string;

class TransposeFasta
{
public: 
  TransposeFasta();
   TransposeFasta( SeqReaderFile* pReader );
    ~TransposeFasta();

    bool convert( const string& input,const string& output );   //Input from Fasta file (converts Fasta File into cyc Files)
	bool inputCycFile();									//Input from cyc files
	bool convertFromCycFileToFasta(const string& fileOutput,  dataTypeNSeq, dataTypelenSeq);		//Convert cyc files into Fasta File

	dataTypelenSeq lengthRead;    //Lenght of each text
	dataTypeNChar lengthTexts;   //Total length of all texts without $-symbols

	dataTypeNSeq nSeq;   //number total of texts in filename1
	dataTypeNChar freq[256];  //contains the distribution of the symbols. It is useful only for testing. It depends on the #characters
    
private:
	SeqReaderFile* pReader_;
	uint cycleNum_;
	vector<FILE*> outputFiles_;
	vector<vector<uchar> > buf_;
	//    FILE* outputFiles_[CYCLENUM];

    //    uchar buf_[CYCLENUM][BUFFERSIZE];
};



#endif
