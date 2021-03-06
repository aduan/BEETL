/******************************************************************************
 *   Copyright (C) 2010											              *
 *                                                                            *
 *                                                                            *
 *   This program is free software; you can redistribute it and/or modify     *
 *   it under the terms of the GNU Lesser General Public License as published *
 *   by the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                      *
 *                                                                            *
 *   This program is distributed in the hope that it will be useful,          *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of           *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
 *   GNU Lesser General Public License for more details.                      *
 *                                                                            *
 *   You should have received a copy of the GNU Lesser General Public License *
 *   along with this program; if not, write to the                            *
 *   Free Software Foundation, Inc.,                                          *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.            *
 *****************************************************************************/

#include "BCRexternalBWT.h"
#include "BWTCollection.h"
#include "TransposeFasta.h"
#include "Tools.h"
//#include "Timer.hh"
#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <stdio.h>
#include <assert.h>
#include <sys/stat.h>
#include <math.h>
#include <cstring>

//#include "Sorting.h"
#define SIZEBUFFER 1024
#define DIMBLOCK 2048

//using std::vector;
using namespace std;
using SXSI::BWTCollection;

////////////////////////////////////////////////////////////////////////////
// Class BCRexternalBWT

/**
 * Constructor inits
 */
BCRexternalBWT::BCRexternalBWT
(char *file1, char *fileOutput, int mode, 
 CompressionFormatType outputCompression)
{
	const char* fileOut = "cyc.";
	if (mode == 0) {
	  //	  assert(1==0);
		std::cerr << "Start BCR encode\n";
		int result = -1;

		outputCompression_=outputCompression;
		std::cerr << "Compression format for BWT output files: ";
		if (outputCompression_==compressionASCII)
		  std::cerr << " ASCII";
		else if (outputCompression_==compressionRunLength)
		  std::cerr << " run-length";
		else if (outputCompression_==compressionHuffman)
		  std::cerr << " Huffman";
		std::cerr << std::endl;

		//added by GIOVANNA
		if (BUILD_SA==1)
			std::cerr << "Compute also the SA by using the BCR (BCR_SA)\n";
		else
			std::cerr << "Compute only the BWT by using the BCR  (BCR_SA)\n";

		//it makes sure that this files does not exist.
		remove("outFileEndPos.bwt");      			//std::cerr << "outFileEndPos.bwt" <<": Error deleting file" << std::endl;

		result = buildBCR(file1, fileOut);
        checkIfEqual(result,1);

//#ifdef XXX		
		//Store the entire BWT from alphabetSize-files
		storeEntireBWT(fileOutput);
//#endif

		//Do we want compute the extended suffix array (position and number of sequence)?
		if (BUILD_SA == 1) { 	//To store the SA
			storeEntirePairSA(fileOutput);
			storeEntireSAfromPairSA(fileOutput);
		}

		if (verboseEncode == 1) {
			if ((BUILD_SA==1)) {
				std::cerr << "Store the files containing the BWT and SA in a single file\n";
				int lung = strlen(fileOutput);
				char *fnSA = new char[lung+3];
				char *fnPairSA = new char[lung+7];
				char *fileOutRes = new char[lung+4];
				dataTypeNChar numchar;
				numchar=sprintf (fnSA,"%s%s",fileOutput,".sa");
				numchar=sprintf (fnPairSA,"%s%s",fileOutput,".pairSA");	
				numchar=sprintf (fileOutRes,"%s%s",fileOutput,".txt");

				FILE *OutFile = fopen(fileOutRes, "w");
				if (OutFile==NULL) {
					std::cerr << "Error opening output \"" << fileOutRes << "\" file"<< std::endl;
					exit (EXIT_FAILURE);
				}

				
				char *fileEndPos="outFileEndPos.bwt";
				static FILE *InFileEndPos;                  // input file of the end positions;
				InFileEndPos = fopen(fileEndPos, "rb");
				if (InFileEndPos==NULL) {
					std::cerr << "decodeBCRnaiveForward: could not open file " << fileEndPos << " !" << std::endl; 
					exit (EXIT_FAILURE);
				}
				dataTypeNSeq numTexts;
				numchar = fread (&numTexts, sizeof(dataTypeNChar), 1 , InFileEndPos);
				checkIfEqual( numchar , 1); // we should always read the same number of characters
				checkIfEqual( numTexts , nText); // we should always read the same number of characters

				sortElement triple;
				fprintf(OutFile, "Position of end-markers\n");
				fprintf(OutFile, "seqN \t posN \t pileN\n");
				for (dataTypeNSeq i = 0; i < nText; i++) {
					numchar = fread (&triple.seqN, sizeof(dataTypeNSeq), 1 , InFileEndPos); 
					assert( numchar == 1); // we should always read the same number of characters
					numchar = fread (&triple.posN, sizeof(dataTypeNChar), 1 , InFileEndPos);    //it is the relative position of the $ in the partial BWT 
					assert( numchar == 1); // we should always read the same number of characters
					numchar = fread (&triple.pileN, sizeof(dataTypedimAlpha), 1 , InFileEndPos);    
					assert( numchar == 1); // we should always read the same number of characters
					fprintf(OutFile, "%d\t%d\t%d\n", triple.seqN, triple.posN, (int)triple.pileN);
					//std::cerr << std::endl << "Starting Tripla: " << triple.seqN << " " << triple.posN << " " << (int)triple.pileN << "\n" << std::endl;
				}

				fprintf(OutFile, "\n");

				FILE *InFileBWT = fopen(fileOutput, "rb");
				if (InFileBWT==NULL) {
					std::cerr << "Entire BWT file: Error opening "  << fileOutput << std::endl;
					exit (EXIT_FAILURE);
				}


				FILE *InFilePairSA = fopen(fnPairSA, "rb");
				if (InFilePairSA==NULL) {
					std::cerr << "Entire Pairs SA file: Error opening " << fnPairSA << std::endl;
					exit (EXIT_FAILURE);
				}

				FILE* InFileSA = fopen(fnSA, "rb");
				if (InFileSA==NULL) {
					std::cerr << "Entire SA file: Error opening " << fnSA << std::endl;
					exit (EXIT_FAILURE);

				}
								
				uchar *bufferBWT = new uchar[SIZEBUFFER];
				ElementType *buffer = new ElementType[SIZEBUFFER];
				dataTypeNChar *bufferNChar = new dataTypeNChar[SIZEBUFFER];

				while ((!feof(InFileBWT)) && (!feof(InFileSA)) && (!feof(InFilePairSA))) {
					dataTypeNChar numcharBWT = fread(bufferBWT,sizeof(uchar),SIZEBUFFER,InFileBWT);
					dataTypeNChar numcharPairSA = fread(buffer,sizeof(ElementType),SIZEBUFFER,InFilePairSA);
					dataTypeNChar numcharSA = fread(bufferNChar,sizeof(dataTypeNChar),SIZEBUFFER,InFileSA);
					//std::cerr << "Char read: " << numcharBWT  << "\t" << numcharSA << "\t" << numcharPairSA  << "\n";
					fprintf(OutFile, "bwt\tpos\tnumSeq\tSA\n");
					if ((numcharPairSA != numcharSA) || (numcharBWT != numcharSA))
						std::cerr << "Error: number  in BWT in Pair SA in SA\n";
					else {
						for (dataTypeNChar i=0; i < numcharSA; i++) {
							//std::cerr << (int)buffer[i].sa << "\t"<< buffer[i].numSeq << "\t" << bufferNChar[i] << "\n";
     						fprintf(OutFile, "%c\t%d\t%d\t%lu\n", bufferBWT[i], buffer[i].sa, buffer[i].numSeq, bufferNChar[i]);

						}
					}
				}
				delete[] fnSA;
				delete[] fnPairSA;
				delete[] fileOutRes; 
				delete[] buffer;
				delete[] bufferNChar;

				fclose(InFilePairSA);
				fclose(InFileSA);
				fclose(OutFile);
			}
		}

		if (deleteCycFile == 1)  {
			std::cerr << "Removing the auxiliary input file (cyc files)\n";
			char *filename1;
			filename1 = new char[strlen(fileOut)+sizeof(dataTypelenSeq)*8];

			// delete output files
			for(dataTypelenSeq i=0;i<lengthRead;i++ ) {
				sprintf (filename1, "%s%u.txt", fileOut, i);
				if (remove(filename1)!=0) 
					std::cerr << filename1 <<" BCRexternalBWT: Error deleting file" << std::endl;
			}
			delete [] filename1;
		}

		char *filenameIn = new char[12];
		char *filename = new char[8];
		const char *ext = "";
		std::cerr << "Removing/Renaming the BWT segments\n";
		for (dataTypedimAlpha g = 0 ; g < alphabetSize; g++) {  
			int numchar=sprintf (filename, "%d", g);
			numchar=sprintf (filenameIn,"%s%s",filename,ext);
			if (deletePartialBWT == 1)  {
				if (remove(filenameIn)!=0) 
					std::cerr << "BCRexternalBWT: Error deleting file" << std::endl;
			} 
			else //renome the aux bwt file
			{
				int lung = strlen(fileOutput) + strlen(filenameIn)+1;
				char *newfilename = new char[lung];
				numchar=sprintf (newfilename,"%s%s",fileOutput,filenameIn);
				//std::cerr  << newfilename << " " << filenameIn << std::endl;
				if(rename(filenameIn, newfilename))
					std::cerr  <<"BCRexternalBWT: Error renaming file" << std::endl;
			}
		}
/*		std::cerr << "Removing/Renaming the SA segments\n";
		for (dataTypedimAlpha g = 0 ; g < alphabetSize; g++) {  
			int numchar=sprintf (filename, "sa_%d", g);
			numchar=sprintf (filenameIn,"%s%s",filename,ext);
			if (deletePartialSA == 1)  {
				if (remove(filenameIn)!=0) 
					std::cerr << "BCRexternalBWT: Error deleting file" << std::endl;
			} 
			else //renome the aux bwt file
			{
				int lung = strlen(fileOutput) + strlen(filenameIn)+1;
				char *newfilename = new char[lung];
				numchar=sprintf (newfilename,"%s%s",fileOutput,filenameIn);
				//std::cerr  << newfilename << " " << filenameIn << std::endl;
				if(rename(filenameIn, newfilename))
					std::cerr  <<"BCRexternalBWT: Error renaming file" << std::endl;
			}
		}
*/
		delete [] filenameIn;
		delete [] filename;
	}
	else if (mode == 1) {
		std::cerr << "Start BCR decode\n";
		const char* fileOutBwt = "";
		int result = -1;
		result = unbuildBCR(file1, fileOutBwt, fileOut, fileOutput);
        checkIfEqual(result,1);
	}
	else if (mode == 2) {

		std::cerr << "Start Locate Function:\n";
		std::cerr << "Backward Search and Recover of the number of sequences\n";
		const char* fileOutBwt = "";
		
		vector<string> kmers;
		char kmer[101];
		for (int i=0; i< 101; i++)
			kmer[i]='\0';
		dataTypelenSeq lenKmer = 0;

		static FILE *InFileKmer;                  
		InFileKmer = fopen(fileOutput, "rb");
		if (InFileKmer==NULL) {
			 std::cerr << "Error opening \"" << fileOutput << "\" file"<< std::endl;
			exit (1);
		}
		while (fgets(kmer, sizeof(kmer), InFileKmer)) { 
			char *tmp = strchr(kmer, '\n');
			if (tmp) 
				*tmp = '\0';
			tmp = strchr(kmer, '\r');
			if (tmp) 
				*tmp = '\0';
			
                        
                        
			if ((strcmp( kmer, "\r") != 0) && (strcmp( kmer, "\n") != 0)
                            && (strcmp( kmer, "\0") != 0)) {
				kmers.push_back(kmer);
				lenKmer = strlen(kmer);			
			}
		}
		fclose(InFileKmer); 
		
		
	
		int result = -1;
		vector <int> seqID;
		result = SearchAndLocateKmer(file1, fileOutBwt, fileOut, kmers, lenKmer, &seqID);
                checkIfEqual(result,1);
		std::cerr << "\nBCRexternalBWT: We have located all kmers, Now we store the positions of the found kmers";		
		if (seqID.size()==0)
			std::cerr << "\nBCRexternalBWT: all k-mers don't occur in the collection";
		else
		{
			dataTypeNChar numchar=0;
			char *newfilename = new char[strlen(fileOutput) + strlen("_positionsKmers")+1];
			numchar=sprintf (newfilename,"%s_positionsKmers",fileOutput);
			static FILE *FilePosKmers;
			FilePosKmers = fopen(newfilename, "wb");
			if (FilePosKmers==NULL) {
				std::cerr << "BCRexternalBWT: could not open file " << newfilename << "!" << std::endl; 
				exit (EXIT_FAILURE);
			}
			fprintf(FilePosKmers, "kmer_ID \t N_kmer \t pos_in_the_SA\n");
			dataTypeNSeq numTotKmers=0;
			for (dataTypeNSeq g = 0 ; g < kmers.size(); g++) {
				for (dataTypeNChar  j = FirstVector[g].posN ; j <= LastVector[g].posN; j++) { //For each position between first and last
					fprintf(FilePosKmers, "%u \t %u \t %d\n", LastVector[g].seqN, numTotKmers, seqID[numTotKmers]);
					numTotKmers++;
				}
			}
			fclose(FilePosKmers);
			delete[] newfilename;

		/*	if (verboseDecode == 1) {
				dataTypeNSeq numTotKmers=0;
				for (dataTypeNSeq g = 0 ; g < kmers.size(); g++) {
					std::cerr << "\nk-mer of index " << LastVector[g].seqN << ": "<< kmers[LastVector[g].seqN] << ". Number of occurrences " << LastVector[g].posN-FirstVector[g].posN+1 << std::endl;
					for (dataTypeNChar  j = FirstVector[g].posN ; j <= LastVector[g].posN; j++) { //For each position between first and last
						std::cerr << "Number " << numTotKmers << " pos in the SA=\t"<< j << "\t SeqId=\t" << seqID[numTotKmers] <<  std::endl;
						numTotKmers++;
					}
				}
			}
			*/

		}
	}		
	else
		std::cerr << "Mode Error \n";
}

int BCRexternalBWT::SearchAndLocateKmer (char const* file1, char const* fileOutBwt, char const * fileOut, vector<string> kmers, dataTypelenSeq lenKmer, vector <int>* seqID) {
		dataTypeNChar freq[256];  //contains the distribution of the symbols.
		int resultInit = initializeUnbuildBCR(file1, fileOutBwt, freq);
		checkIfEqual(resultInit,1);
		
		std::cerr << "Frequency"  << "\n";
		for (dataTypedimAlpha i = 0; i < 255; i++)
			if (freq[i] > 0) {	
				std::cerr << i << "\t" << freq[i] << "\t" << (int)alpha[i] << "\t" << (int)alphaInverse[(int)alpha[i]] << "\n";
		}

		if (BackByVector == 1) {
			resultInit = computeVectorUnbuildBCR(file1, fileOutBwt, freq);
                        checkIfEqual(resultInit,1);
		}

		std::cerr << "backwardSearchManyBCR\n";

		int result = -1;
		result = backwardSearchManyBCR(file1, fileOutBwt, fileOut, kmers, lenKmer);
		
		for (dataTypeNSeq g = 0 ; g < kmers.size(); g++)   {
			std::cerr << "The number of occurrences of "<< kmers[LastVector[g].seqN] << " is \t" << LastVector[g].posN - FirstVector[g].posN + 1<< "\n";
		}
		
		if (verboseDecode==1) {
			std::cerr << "First and Last: "  <<  "\n";
			std::cerr << "Q  ";
			for (dataTypeNSeq g = 0 ; g < kmers.size(); g++) {  
				std::cerr << (int)FirstVector[g].pileN << " " << (int)LastVector[g].pileN << "\t";
			}
			std::cerr << std::endl;
			std::cerr << "P  ";
			for (dataTypeNSeq g = 0 ; g < kmers.size(); g++) {  
					std::cerr << FirstVector[g].posN  << " " << LastVector[g].posN  << "\t";
			}
			std::cerr << std::endl;
			std::cerr << "N  ";
			for (dataTypeNSeq g = 0 ; g < kmers.size(); g++) {  
				std::cerr << FirstVector[g].seqN  << " " << LastVector[g].seqN  << "\t" ;
			}
			std::cerr << std::endl;
		}
		
		*seqID = recoverNSequenceForward(file1, fileOutBwt, kmers.size());
		if ((*seqID).size()==0)
			std::cerr << "\nSearchAndLocateKmer: all k-mers don't occur in the collection";

		//result = recoverNSequenceForwardSequentially(file1, fileOutBwt, kmers.size());
		//assert (result ==1);
	
		//Free the memory
		for (dataTypedimAlpha j = 0 ; j < sizeAlpha; j++) { 
			delete [] tableOcc[j];
			tableOcc[j] = NULL;
		}
		delete [] tableOcc;
	
		delete[] alphaInverse;

		return 1;
}

//Computes the rank-inverse function for many sequences by using the vector and update posN with the number of symbols that it read.
//Computes the position of the i-th occurrences of the symbol toFindSymbolp[h] in the BWT.
//posN[h] is the number of occurrences of the symbol toFindSymbol[h] that I have to find in BWT corresponding to the i-th occurrence of the symbol in F.
int BCRexternalBWT::rankInverseManyByVector (char const* file1, char const* fileOutBwt, dataTypeNSeq numKmersInput, uchar *toFindSymbols)
{
	dataTypeNChar numchar=0;
	char *filenameIn = new char[12];
	char *filename = new char[8];
	const char *ext = "";
	numchar=0;
	static FILE *InFileBWT;
	uchar *buf = new uchar[SIZEBUFFER];

	//Timer timer;
	
	dataTypeNSeq j = 0;
	while (j < numKmersInput) {
		//std::cerr << "===j= " << j << " vectTriple[j].pileN " << (int)vectTriple[j].pileN << " vectTriple[j].seqN " << vectTriple[j].seqN <<"\n";
		//We work into one BWT-partial at the time.
		dataTypedimAlpha currentPile = vectTriple[j].pileN;
		numchar=sprintf (filename, "%s%d", fileOutBwt, currentPile);
		numchar=sprintf (filenameIn,"%s%s",filename,ext);
		//#ifdef DEBUG
		//	std::cerr << "===Current BWT-partial= " << (int)currentPile << "\n";
		//#endif
		char *newfilename = new char[strlen(file1) + strlen(filenameIn)+1];
		numchar=sprintf (newfilename,"%s%s",file1,filenameIn);
		InFileBWT = fopen(newfilename, "rb");
		if (InFileBWT==NULL) {
			std::cerr << "rankInverseManyByVector: BWT file " << (int)j << ": Error opening " << std::endl;
			exit (EXIT_FAILURE);
		}
		//contaAperturaFile++;

		dataTypeNSeq k=j;
		//For each tripla in the same current pile, we have to find the position of toRead-th occurrences of the symbol toFindSymbol
		while ((k < numKmersInput) && (vectTriple[k].pileN == currentPile)) {
			uchar toFindSymbol = toFindSymbols[k];
			//std::cerr << "===k= " << k << " vectTriple[k].pileN " << (int)vectTriple[k].pileN << " vectTriple[k].seqN " << vectTriple[k].seqN << " toFindSymbol " << toFindSymbol <<"\n";
			if (toFindSymbol != TERMINATE_CHAR) {
				
				//Update each tripla, so posN is the number that we should read in a particular block into BWT-partial
				dataTypeNChar readChar=0;
				dataTypeNChar toRead = vectTriple[k].posN;
				dataTypeNChar numBlock = 0;
				//Find the block
				//std::cerr << "toRead "<< toRead << " numBlock " << numBlock << " currentPile " << (int)currentPile << " toFindSymbol " << toFindSymbol<< "\n";
				//cerr << "UPDATE_POS...";
				//timer.timeNow();
				int result = update_Pos_Pile_Blocks(&toRead, &numBlock, currentPile, toFindSymbol); 
				//cerr << "done." << timer << endl;
                                checkIfEqual(result,1);
				readChar = numBlock*DIMBLOCK;
				//We have read readChar by using vectorOcc, now we have to read toRead symbols from the block numBlock
				//We move the file pointer in the position where the numBlock block starts.
				fseek (InFileBWT, numBlock*DIMBLOCK, 0);
				//Find the occurrences in the found block
				dataTypeNChar num = 0, num_read = 0;
				while ((!feof(InFileBWT)) && (toRead > 0)) 	{   
					num_read = fread(buf,sizeof(uchar),SIZEBUFFER,InFileBWT);
					num = 0;
					while ((num < num_read) && (toRead > 0)) {
						if (buf[num] == toFindSymbol)
							toRead--;
						readChar++;       //it is the number of read symbols
						num++;
					}				
					if (toRead < 0)
						std::cerr << "rankInverseManyByVector: position of the symbol not found" << "\n";
				}
				if (toRead > 0) {
					std::cerr << "*Error rankInverseManyByVector: we should read " << toRead << " characters yet in " << newfilename << " file!\n";
					exit (EXIT_FAILURE);
				}
				//Update the value of posN 
				vectTriple[k].posN = readChar;
			}
			k++;
		}
		j = k;
		fclose(InFileBWT);
		delete [] newfilename;
	}
	
	delete [] buf;
	delete [] filenameIn;
	delete [] filename; 

	return 1;
}

//Computes the rank-inverse function and returns the number of symbols that it read.
//Computes the position of the i-th occurrences of the symbol toFindSymbol in the BWT.
//toRead is the number of occurrences of the symbol toFindSymbol that I have to find in BWT corresponding to the i-th occurrence of the symbol in F.
dataTypeNChar BCRexternalBWT::findRankInBWT (char const* file1, char const* fileOutBwt, dataTypedimAlpha currentPile, dataTypeNChar toRead, uchar toFindSymbol)
{
	char *filenameIn = new char[12];
	char *filename = new char[8];
	const char *ext = "";
	
	dataTypeNChar numchar=0;

	numchar=sprintf (filename, "%s%d", fileOutBwt, currentPile);
	numchar=sprintf (filenameIn,"%s%s",filename,ext);
	char *newfilename = new char[strlen(file1) + strlen(filenameIn)+1];
	numchar=sprintf (newfilename,"%s%s",file1,filenameIn);

	static FILE *InFileBWT;

	uchar *buf = new uchar[SIZEBUFFER];
	
	InFileBWT = fopen(newfilename, "rb");
	if (InFileBWT==NULL) {
		std::cerr << "findRankInBWT: could not open file " << newfilename << " !" << std::endl; 
		exit (EXIT_FAILURE);
	}

	dataTypeNChar num = 0, num_read = 0, readChar=0;
	//#ifdef DEBUG
	//	std::cerr << "***FindRankInBWT: we must to read " << toRead << " occurrences of the symbol " << toFindSymbol << "!\n";
	//#endif

	while ((!feof(InFileBWT)) && (toRead > 0)) 	{   
		num_read = fread(buf,sizeof(uchar),SIZEBUFFER,InFileBWT);
		num = 0;
		while ((num < num_read) && (toRead > 0)) {
			if (buf[num] == toFindSymbol)
				toRead--;
			readChar++;       //it is the number of read symbols
			num++;
		}				
		if (toRead < 0)
			std::cerr << "findRankInBWT: position of the symbol not found" << "\n";
	}

	if (toRead > 0) {
		std::cerr << "Error findRankInBWT: we should read " << toRead << " characters yet in " << newfilename << " file!\n";
		exit (EXIT_FAILURE);
	}
	fclose(InFileBWT);
	delete [] buf;
	delete [] filenameIn;
	delete [] filename; 
	delete [] newfilename;
	return readChar;
}

//Computes the rank-inverse function and returns the number of symbols that it read.
//Computes the position of the i-th occurrences of the symbol toFindSymbol in the BWT.
//toRead is the number of occurrences of the symbol toFindSymbol that I have to find in BWT corresponding to the i-th occurrence of the symbol in F.
dataTypeNChar BCRexternalBWT::findRankInBWTbyVector (char const* file1, char const* fileOutBwt, dataTypedimAlpha currentPile, dataTypeNChar toRead, uchar toFindSymbol)
{
	char *filenameIn = new char[12];
	char *filename = new char[8];
	const char *ext = "";
	
	dataTypeNChar numchar=0;

	numchar=sprintf (filename, "%s%d", fileOutBwt, currentPile);
	numchar=sprintf (filenameIn,"%s%s",filename,ext);
	char *newfilename = new char[strlen(file1) + strlen(filenameIn)+1];
	numchar=sprintf (newfilename,"%s%s",file1,filenameIn);

	static FILE *InFileBWT;
	uchar *buf = new uchar[SIZEBUFFER];
	
	InFileBWT = fopen(newfilename, "rb");
	if (InFileBWT==NULL) {
		std::cerr << "findRankInBWTbyVector: could not open file " << newfilename << " !" << std::endl; 
		exit (EXIT_FAILURE);
	}

	dataTypeNChar readChar=0;
	dataTypeNChar numBlock = 0;

	int result = update_Pos_Pile_Blocks(&toRead, &numBlock, currentPile, toFindSymbol);
        checkIfEqual(result,1);
	readChar = numBlock*DIMBLOCK;
	
	fseek (InFileBWT, numBlock*DIMBLOCK, 0);       
	
	dataTypeNChar num = 0, num_read = 0;
	while ((!feof(InFileBWT)) && (toRead > 0)) 	{   
		num_read = fread(buf,sizeof(uchar),SIZEBUFFER,InFileBWT);
		num = 0;
		while ((num < num_read) && (toRead > 0)) {
			if (buf[num] == toFindSymbol)
				toRead--;
			readChar++;       //it is the number of read symbols
			num++;
		}				
		if (toRead < 0)
			std::cerr << "findRankInBWTbyVector: position of the symbol not found" << "\n";
	}

	if (toRead > 0) {
		std::cerr << "*Error findRankInBWTbyVector: we should read " << toRead << " characters yet in " << newfilename << " file!\n";
		exit (EXIT_FAILURE);
	}
	fclose(InFileBWT);
	delete [] buf;
	delete [] filenameIn;
	delete [] filename; 
	delete [] newfilename;

	return readChar;
}

//Computes the rank function and returns the number of symbols that it read.
//The rank function computes the number char less than the symbol c from the starting position (startPos) in the BWT to the position pos (endPos) in the BWT.
//Here, we compute the number of occurrences of each symbol from from the starting position (startPos) in the BWT to the position pos (endPos) in the BWT.
//The startPos is the position of the File pointer InFileBWT, the endPos depends on toRead
//In the original definition of the rank, startPos corresponds to the position 1 and endPos corresponds to the previous symbol.
//Here, we work by using \sigma partial BWTs.
//toRead is the number of symbols that I have to read before to find the symbol in B corresponding to the symbol in F.
dataTypeNChar BCRexternalBWT::rankManySymbols(FILE & InFileBWT, dataTypeNChar *counters, dataTypeNChar toRead, uchar *foundSymbol)
{	
	dataTypeNChar numchar, cont=0;  //cont is the number of symbols already read!
	uchar *buffer = new uchar[SIZEBUFFER];

	//it reads toRead symbols from the fp file (Partial BWT) 
	while (toRead > 0) {            //((numchar!=0) && (toRead > 0)) {
		if (toRead <= SIZEBUFFER) {    //Read toRead characters           
			numchar = fread(buffer,sizeof(uchar),toRead,&InFileBWT);
			// we should always read/write the same number of characters
                        checkIfEqual(numchar,toRead);
			*foundSymbol = buffer[numchar-1];     //The symbol of the sequence k.  It is the symbol in the last position in the partial BWT that we have read.
		}
		else {   //Read sizebuffer characters
			numchar = fread(buffer,sizeof(uchar),SIZEBUFFER,&InFileBWT);
			// we should always read/write the same number of characters
                        checkIfEqual(numchar,SIZEBUFFER);
		}
			
		//For each symbol in the buffer, it updates the number of occurrences into counters
		for (dataTypeNChar r=0; r<numchar; r++) 	
			counters[alpha[(int)buffer[r]]]++;    //increment the number of letter symbol into counters
		

		cont   += numchar;  //number of read symbols
		toRead -= numchar;  //number of remaining symbols to read 
		if ((numchar == 0) && (toRead > 0)) {  //it means that we have read 0 character, but there are still toRead characters to read
			std::cerr << "rankManySymbols: read 0 character, but there are still " << toRead << " characters to read  " << std::endl;
			exit (EXIT_FAILURE);
		}
	}
	delete [] buffer;
	
	return cont;  
}

dataTypeNChar BCRexternalBWT::rankManySymbolsByVector(FILE & InFileBWT, dataTypeNChar *counters, dataTypeNChar toRead, uchar *foundSymbol)
{	
	dataTypeNChar numchar, cont=0;  //cont is the number of symbols already read!
	uchar *bufferBlock = new uchar[DIMBLOCK];

	//it reads toRead symbols from the fp file (Partial BWT) 
	while (toRead > 0) {            //((numchar!=0) && (toRead > 0)) {
		if (toRead <= DIMBLOCK) {    //Read toRead characters           
			numchar = fread(bufferBlock,sizeof(uchar),toRead,&InFileBWT);
			checkIfEqual(numchar,toRead); // we should always read/write the same number of characters
                        
			*foundSymbol = bufferBlock[numchar-1];     //The symbol of the sequence k.  It is the symbol in the last position in the partial BWT that we have read.
		}
		else {   //Read sizebuffer characters
			std::cerr << "rankManySymbolsByVector: Error to read is" << toRead << std::endl;
			exit (EXIT_FAILURE);
			//numchar = fread(buffer,sizeof(uchar),SIZEBUFFER,&InFileBWT);
			//assert(numchar == SIZEBUFFER); // we should always read/write the same number of characters
			//aggiorna counters dalla tabella del vettori
		}
			
		//For each symbol in the buffer, it updates the number of occurrences into counters
		for (dataTypeNChar r=0; r<numchar; r++) 	
			counters[alpha[(int)bufferBlock[r]]]++;    //increment the number of letter symbol into counters
		

		cont   += numchar;  //number of read symbols
		toRead -= numchar;  //number of remaining symbols to read 
		if ((numchar == 0) && (toRead > 0)) {  //it means that we have read 0 character, but there are still toRead characters to read
			std::cerr << "rankManySymbolsByVector: read 0 character, but there are still " << toRead << " characters to read  " << std::endl;
			exit (EXIT_FAILURE);
		}
	}
	delete [] bufferBlock;
	
	return cont;  
}


int BCRexternalBWT::computeNewPositonForBackSearch(char const* file1, char const* fileOutBwt, uchar symbol)
{
	dataTypeNChar numchar=0;
	char *filenameIn = new char[12];
	char *filename = new char[8];
	const char *ext = "";
	numchar=0;
	FILE *InFileBWT;
	
	//Last = C[c] + rank (c, Last)				--> (vectTriple[1].pileN, vectTriple[1].posN)

	//First = C[c] + rank (c, First - 1) + 1    --> (vectTriple[0].pileN, vectTriple[0].posN)
	//So we write:
	vectTriple[0].posN --;   //So we compute rank until position First - 1  

	uchar foundSymbol = '\0';
	dataTypeNChar toRead = 0;
	dataTypeNChar *counters = new dataTypeNChar[sizeAlpha];  //it counts the number of each symbol into the i-Pile-BWT
	dataTypeNSeq j = 0;
	while (j < 2) {			
		for (dataTypedimAlpha i = 0 ; i < sizeAlpha; i++)
			counters[i]=0;

		dataTypedimAlpha currentPile = vectTriple[j].pileN;
		numchar=sprintf (filename, "%s%d", fileOutBwt,currentPile);
		numchar=sprintf (filenameIn,"%s%s",filename,ext);
		//if (verboseDecode == 1)
		//	std::cerr << "\n===Current BWT-partial= " << (int)currentPile << "\n";
		
		char *newfilename = new char[strlen(file1) + strlen(filenameIn)+1];
		numchar=sprintf (newfilename,"%s%s",file1,filenameIn);
		InFileBWT = fopen(newfilename, "rb");
		if (InFileBWT==NULL) {
			std::cerr << "computeNewPositonForBackSearch: BWT file " << (int)j << ": Error opening " << std::endl;
			exit (EXIT_FAILURE);
		}
		dataTypeNSeq k=j;
		dataTypeNChar cont = 0;   //number of the read symbols
		dataTypeNChar numberRead=0;
		//uchar symbol;
		//dataTypelenSeq lenCheck=0;
		while ((k< 2) && (vectTriple[k].pileN == currentPile)) {
			//The symbol for the sequences seqN in F[posN]  is the symbol
			//symbol = alphaInverse[vectTriple[k].pileN];
			//Now, I have to find the new symbol, it is in B[pileN] in position posN and so I can update pileN and posN
			
			//For any character (of differents sequences) in the same pile
			//symbol = '\0';
			//cont is the number of symbols already read!
			toRead = vectTriple[k].posN - cont;
			numberRead = rankManySymbols(*InFileBWT, counters, toRead, &foundSymbol);
			checkIfEqual(toRead,numberRead);
			cont += numberRead;

			//I have to update the value in vectTriple[k].posN, it must contain the position of the symbol in F
			//Symbol is
			//newSymb[vectTriple[k].seqN] = symbol;   //it is not useful here
			//PosN is
			vectTriple[k].posN = counters[alpha[(int)symbol]];   
			for (dataTypedimAlpha g = 0 ; g < currentPile; g++) {  //I have to count in each pile g= 0... (currentPile-1)-pile
				vectTriple[k].posN = vectTriple[k].posN + tableOcc[g][alpha[(int)symbol]];
			}
			//pileN is
			vectTriple[k].pileN=alpha[(int)symbol];
			k++;
		}
		fclose(InFileBWT);
		
		j=k;
		delete [] newfilename;
	}
	

	delete [] counters;
	delete [] filenameIn;
	delete [] filename;

	//First = c[c] + rank (c, First - 1) + 1
	vectTriple[0].posN ++;  //We must to sum 1 to first

	return 1;
}

int BCRexternalBWT::computeNewPositonForBackSearchByVector(char const* file1, char const* fileOutBwt, uchar symbol)
{
	dataTypeNChar numchar=0;
	char *filenameIn = new char[12];
	char *filename = new char[8];
	const char *ext = "";
	numchar=0;
	FILE *InFileBWT;
	
	//Last = C[c] + rank (c, Last)				--> (vectTriple[1].pileN, vectTriple[1].posN)

	//First = C[c] + rank (c, First - 1) + 1    --> (vectTriple[0].pileN, vectTriple[0].posN)
	//So we write:
	vectTriple[0].posN --;   //So we compute rank until position First - 1  

	uchar foundSymbol = '\0';
	dataTypeNChar toRead = 0;
	dataTypeNChar *counters = new dataTypeNChar[sizeAlpha];  //it counts the number of each symbol into the i-Pile-BWT
	dataTypeNSeq j = 0;
	while (j < 2) {			
			//The symbol for the sequences seqN in F[posN]  is the symbol
			//symbol = alphaInverse[vectTriple[k].pileN];
			//Now, I have to find the new symbol, it is in B[pileN] in position posN and so I can update pileN and posN
		for (dataTypedimAlpha i = 0 ; i < sizeAlpha; i++)
			counters[i]=0;

		dataTypedimAlpha currentPile = vectTriple[j].pileN;
		numchar=sprintf (filename, "%s%d", fileOutBwt,currentPile);
		numchar=sprintf (filenameIn,"%s%s",filename,ext);
		//if (verboseDecode == 1)
		//	std::cerr << "\n===Current BWT-partial= " << (int)currentPile << "(computeNewPositonForBackSearchByVector)\n";
		
		char *newfilename = new char[strlen(file1) + strlen(filenameIn)+1];
		numchar=sprintf (newfilename,"%s%s",file1,filenameIn);
		InFileBWT = fopen(newfilename, "rb");
		if (InFileBWT==NULL) {
			std::cerr << "computeNewPositonForBackSearchByVector: BWT file " << (int)j << ": Error opening " << std::endl;
			exit (EXIT_FAILURE);
		}
		dataTypeNSeq k=j;
		dataTypeNChar cont = 0;   //number of the read symbols
		dataTypeNChar numberRead=0;
		//uchar symbol;
		//dataTypelenSeq lenCheck=0;
		dataTypeNChar numBlock = 0;
		while ((k< 2) && (vectTriple[k].pileN == currentPile)) {
			//cont is the number of symbols already read!
			//toRead = vectTriple[k].posN - cont;
			toRead = vectTriple[k].posN;
			for (dataTypedimAlpha i = 0 ; i < sizeAlpha; i++)
				counters[i]=0;	

			if (toRead > 0) {
				//we need to know how many occurrences of each symbol there are up to the position toRead.
				//if ToRead > dimBlock, we can use vectorOcc in order to find the occorrences in the blocks precede the block where the position toRead is.
				//Before, we need to find the block where toRead position is.
				int result = findBlockToRead(counters, currentPile, &toRead, &numBlock);
				checkIfEqual(result,1);
			}

			if (toRead <= DIMBLOCK) {   //If toRead == DIMBLOCK, because I can need to known foundSymbol character
				fseek (InFileBWT, numBlock*DIMBLOCK, 0);       
				numberRead = rankManySymbolsByVector(*InFileBWT, counters, toRead, &foundSymbol);
				checkIfEqual(toRead,numberRead);
				cont += numberRead;
			}

			//I have to update the value in vectTriple[k].posN, it must contain the position of the symbol in F
			//Symbol is
			//newSymb[vectTriple[k].seqN] = symbol;   //it is not useful here
			//PosN is
			vectTriple[k].posN = counters[alpha[(int)symbol]];   
			for (dataTypedimAlpha g = 0 ; g < currentPile; g++) {  //I have to count in each pile g= 0... (currentPile-1)-pile
				vectTriple[k].posN = vectTriple[k].posN + tableOcc[g][alpha[(int)symbol]];
			}
			//pileN is
			vectTriple[k].pileN=alpha[(int)symbol];
			k++;
		}
		fclose(InFileBWT);
		
		j=k;
		delete [] newfilename;
	}
	

	delete [] counters;
	delete [] filenameIn;
	delete [] filename;

	//First = c[c] + rank (c, First - 1) + 1
	vectTriple[0].posN ++;  //We must to sum 1 to first

	return 1;
}

int BCRexternalBWT::findBlockToRead(dataTypeNChar *counters, dataTypedimAlpha currentPile, dataTypeNChar *toRead, dataTypeNChar *numBlock) {
	//Find the block numblock, where the position toRead is
	//numBlock = 0;
	*numBlock = (dataTypeNChar)floor((long double)((*toRead-1)/DIMBLOCK)) ;  //The smallest integral value NOT less than x.
	//if (*numBlock >= numBlocksInPartialBWT[currentPile])
	//		std::cerr << "Error findBlockToRead: numBlock " << *numBlock << " and numBlocksInPartialBWT["<<(int)currentPile<<"]" << numBlocksInPartialBWT[currentPile] << "\n";
	//assert(*numBlock < numBlocksInPartialBWT[currentPile]);

        if (*numBlock >= numBlocksInPartialBWT[currentPile]){
            cerr << "Numblock size mismatch: " << *numBlock << " < " 
                 << numBlocksInPartialBWT[currentPile]
                 << ". Aborting." << endl;
        }
        
	if( *numBlock > 0 )
	  {
	    for (dataTypedimAlpha r=0; r<sizeAlpha; r++) 
	      counters[r] =  vectorOcc[currentPile][r][(*numBlock)-1];   //vectorOcc is indexed by 0, so we have numBlock-1
	    *toRead = *toRead - (*numBlock*DIMBLOCK);  //Number of symbols that we must read yet. it could be = DIMBLOCK
	  }
	
	return 1;
}

int BCRexternalBWT::computeManyNewPositonForBackSearchByVector(char const* file1, char const* fileOutBwt, uchar *symbols, dataTypeNSeq nKmers)
{
	dataTypeNChar numchar=0;
	char *filenameIn = new char[12];
	char *filename = new char[8];
	const char *ext = "";
	numchar=0;
	FILE *InFileBWT;
	
	//Last = C[c] + rank (c, Last)				--> (vectTriple[1].pileN, vectTriple[1].posN)

	//First = C[c] + rank (c, First - 1) + 1    --> (vectTriple[0].pileN, vectTriple[0].posN)
	//So we write:
	for (dataTypeNSeq i=0; i<nKmers; i++)    //For each kmer
		if (LastVector[i].posN >= FirstVector[i].posN)   //if not, the kmer is not in the collection
			FirstVector[i].posN --;   //So we compute rank until position First - 1  

	uchar foundSymbol = '\0';  //here, it is not useful
	dataTypeNChar toRead = 0;
	dataTypeNChar *counters = new dataTypeNChar[sizeAlpha];  //it counts the number of each symbol into the i-Pile-BWT
	dataTypeNSeq j = 0;
	while (j < nKmers) {
		//The symbol for the sequences seqN in F[posN]  is the symbol
		//symbol = alphaInverse[vectTriple[k].pileN];
		//Now, I have to find the new symbol, it is in B[pileN] in position posN and so I can update pileN and posN
		for (dataTypedimAlpha i = 0 ; i < sizeAlpha; i++)
			counters[i]=0; 

		dataTypedimAlpha currentPile = FirstVector[j].pileN;
		numchar=sprintf (filename, "%s%d", fileOutBwt,currentPile);
		numchar=sprintf (filenameIn,"%s%s",filename,ext);
		//if (verboseDecode == 1)
		//	std::cerr << "===Current BWT-partial= " << (int)currentPile << " (computeManyNewPositonForBackSearchByVector)\n";
		
		char *newfilename = new char[strlen(file1) + strlen(filenameIn)+1];
		numchar=sprintf (newfilename,"%s%s",file1,filenameIn);
		InFileBWT = fopen(newfilename, "rb");
		if (InFileBWT==NULL) {
			std::cerr << "computeManyNewPositonForBackSearchByVector: BWT file " << (int)j << ": Error opening " << std::endl;
			exit (EXIT_FAILURE);
		}
		dataTypeNSeq k=j;
		//dataTypeNChar cont = 0;   //number of the read symbols
		dataTypeNChar numberRead=0;
		//uchar symbol;
		//dataTypelenSeq lenCheck=0;
		dataTypeNChar numBlock = 0;
		while ((k< nKmers) && (FirstVector[k].pileN == currentPile)) {
			if (FirstVector[k].posN <= LastVector[k].posN) {
				//FIRST
				//cont is the number of symbols already read!
				//toRead = vectTriple[k].posN - cont;
				toRead = FirstVector[k].posN;
				for (dataTypedimAlpha i = 0 ; i < sizeAlpha; i++)
					counters[i]=0;	
				
				if (toRead > 0) {
					//we need to know how many occurrences of each symbol there are up to the position toRead.
					//if ToRead > dimBlock, we can use vectorOcc in order to find the occorrences in the blocks precede the block where the position toRead is.
					//Before, we need to find the block where toRead position is.
					int result = findBlockToRead(counters, currentPile, &toRead, &numBlock);
					checkIfEqual(result,1);
				}

				if (toRead <= DIMBLOCK) {   //If toRead == DIMBLOCK, because I can need to known foundSymbol character
					fseek (InFileBWT, numBlock*DIMBLOCK, 0);      
					numberRead = rankManySymbolsByVector(*InFileBWT, counters, toRead, &foundSymbol);
					checkIfEqual(toRead,numberRead);
					//cont += numberRead;
				}
				//I have to update the value in vectTriple[k].posN, it must contain the position of the symbol in F
				//Symbol is
				//newSymb[vectTriple[k].seqN] = symbol;   //it is not useful here
				//PosN is
				FirstVector[k].posN = counters[alpha[(int)symbols[FirstVector[k].seqN]]];   
				for (dataTypedimAlpha g = 0 ; g < currentPile; g++) {  //I have to count in each pile g= 0... (currentPile-1)-pile
					FirstVector[k].posN = FirstVector[k].posN + tableOcc[g][alpha[(int)symbols[FirstVector[k].seqN]]];
				}
				//pileN is
				FirstVector[k].pileN=alpha[(int)symbols[FirstVector[k].seqN]];
				//First = c[c] + rank (c, First - 1) + 1
				FirstVector[k].posN ++;  //We must to sum 1 to first

				//LAST
				toRead = LastVector[k].posN;
				numBlock = 0;
				for (dataTypedimAlpha i = 0 ; i < sizeAlpha; i++)
					counters[i]=0;	
				
				if (toRead > 0) {
					//we need to know how many occurrences of each symbol there are up to the position toRead.
					//if ToRead > dimBlock, we can use vectorOcc in order to find the occorrences in the blocks precede the block where the position toRead is.
					//Before, we need to find the block where toRead position is.
					int result = findBlockToRead(counters, currentPile, &toRead, &numBlock);
					checkIfEqual(result,1);
				
					if (toRead <= DIMBLOCK) {   //If toRead == DIMBLOCK, because I can need to known foundSymbol character
						fseek (InFileBWT, numBlock*DIMBLOCK, 0);       
						numberRead = rankManySymbolsByVector(*InFileBWT, counters, toRead, &foundSymbol);
						checkIfEqual(toRead,numberRead);
						//cont += numberRead;
					}
				}
				//I have to update the value in vectTriple[k].posN, it must contain the position of the symbol in F
				//Symbol is
				//newSymb[vectTriple[k].seqN] = symbol;   //it is not useful here
				//PosN is
				LastVector[k].posN = counters[alpha[(int)symbols[FirstVector[k].seqN]]];   
				for (dataTypedimAlpha g = 0 ; g < currentPile; g++) {  //I have to count in each pile g= 0... (currentPile-1)-pile
					LastVector[k].posN = LastVector[k].posN + tableOcc[g][alpha[(int)symbols[FirstVector[k].seqN]]];
				}
				//pileN is
				LastVector[k].pileN=alpha[(int)symbols[FirstVector[k].seqN]];
			}
			k++;
		}
		fclose(InFileBWT);
		
		j=k;
		delete [] newfilename;
	}
	

	delete [] counters;
	delete [] filenameIn;
	delete [] filename;

	return 1;
}

int BCRexternalBWT::backwardSearchManyBCR(char const* file1, char const* fileOutBwt, char const * fileOut, vector<string> kmers, dataTypelenSeq lenKmer)
{		
	if (BackByVector == 0) {
			std::cerr << "backwardSearchManyBCR is only implemented by using the sampling." << std::endl;
			exit(1);
		}
		else {
			std::cerr << "For the computation of the new positon useful for BackSearch, it uses a sampling of the occurrences for each segment: " << DIMBLOCK << " size." << std::endl;
		}

	//Initialization
	uchar *symbols = new uchar[kmers.size()];
	FirstVector.resize(kmers.size());
	LastVector.resize(kmers.size());
	for (dataTypeNSeq i=0; i<kmers.size(); i++) 
		std::cerr << kmers[i] << "\n";
		
	for (dataTypeNSeq i=0; i<kmers.size(); i++) {   //For each kmer		
		symbols[i]= kmers[i][lenKmer-1];
		//FIRST 
		FirstVector[i].seqN = i;  //It is not useful
		FirstVector[i].pileN = alpha[int(symbols[FirstVector[i].seqN])];
		FirstVector[i].posN = 1;  //The first occurrence of symbol in F is in the first position in the pile Symbol  
		
		//LAST 
		LastVector[i].seqN = i;  //It is not useful
		LastVector[i].pileN = alpha[int(symbols[LastVector[i].seqN])];
		//The last occurrence of the symbol prevSymbol in F is in the last position in the pile prevSymbol 
		//It also corresponds to C[int(symbol) + 1]
		LastVector[i].posN = 0;
		for (dataTypedimAlpha mm = 0 ; mm < sizeAlpha; mm++) 
			LastVector[i].posN += tableOcc[LastVector[i].pileN][mm];
	}
	/*
	if (verboseDecode==1) {
			std::cerr << "Init triples: "  <<  "\n";
			std::cerr << "Symbols in positions " << lenKmer << "\n";
			for (dataTypeNSeq g = 0 ; g < kmers.size(); g++) {  
				std::cerr << symbols[g]  << "\t";
			}
			std::cerr << std::endl;
			std::cerr << "Q  ";
			for (dataTypeNSeq g = 0 ; g < kmers.size(); g++) {  
				std::cerr << (int)FirstVector[g].pileN << " " << (int)LastVector[g].pileN << "\t";
			}
			std::cerr << std::endl;
			std::cerr << "P  ";
			for (dataTypeNSeq g = 0 ; g < kmers.size(); g++) {  
					std::cerr << FirstVector[g].posN  << " " << LastVector[g].posN  << "\t";
			}
			std::cerr << std::endl;
			std::cerr << "N  ";
			for (dataTypeNSeq g = 0 ; g < kmers.size(); g++) {  
				std::cerr << FirstVector[g].seqN  << " " << LastVector[g].seqN  << "\t" ;
			}
			std::cerr << std::endl;
	}
	*/
		
	for (dataTypelenSeq posSymb=lenKmer-1; posSymb>0; posSymb--) {   //For each symbol of the kmer
		
		for (dataTypeNSeq i=0; i<kmers.size(); i++)    //For each kmer in accord to the order in the triples	
			if (LastVector[i].posN >= FirstVector[i].posN)   //if not, the kmer is not in the collection
				symbols[FirstVector[i].seqN]= kmers[FirstVector[i].seqN][posSymb-1];
	
		quickSort(FirstVector);
		quickSort(LastVector);
			
		//For each symbol in the kmer we have to update First and Last
		int resultCompute = computeManyNewPositonForBackSearchByVector (file1, fileOutBwt, symbols, kmers.size());	
		checkIfEqual(resultCompute,1);
		/*		
		if (verboseDecode==1) {
			std::cerr << "After The computation of the new positions: "  <<  "\n";
			std::cerr << "Symbols in positions " << posSymb << "\n";
			for (dataTypeNSeq g = 0 ; g < kmers.size(); g++) {  
				std::cerr << symbols[FirstVector[g].seqN]  << "\t\t";
			}
			std::cerr << std::endl;
			std::cerr << "Q  ";
			for (dataTypeNSeq g = 0 ; g < kmers.size(); g++) {  
				std::cerr << (int)FirstVector[g].pileN << " " << (int)LastVector[g].pileN << "\t";
			}
			std::cerr << std::endl;
			std::cerr << "P  ";
			for (dataTypeNSeq g = 0 ; g < kmers.size(); g++) {  
					std::cerr << FirstVector[g].posN  << " " << LastVector[g].posN  << "\t";
			}
			std::cerr << std::endl;
			std::cerr << "N  ";
			for (dataTypeNSeq g = 0 ; g < kmers.size(); g++) {  
				std::cerr << FirstVector[g].seqN  << " " << LastVector[g].seqN  << "\t" ;
			}
			std::cerr << std::endl;
		}
		*/
	
	}
	
	delete[] symbols;

	return 1;
}

//Reconstruct 1 factor backwards by threading through the LF-mapping.
int BCRexternalBWT::backwardSearchBCR(char const* file1, char const* fileOutBwt, char const * fileOut, char const *kmer)
{
//	dataTypeNChar freq[256];  //contains the distribution of the symbols.
//	int resultInit = initializeUnbuildBCR(file1, fileOutBwt, freq);
//	assert (resultInit == 1);

	std::cerr << "Now: backward search\n";

	dataTypelenSeq lenKmer = strlen(kmer);
	std::cerr << "kmer: " << kmer << " length " << lenKmer << "\n";

	dataTypeNChar posSymb = lenKmer - 1;
	uchar symbol = kmer[posSymb];
	vectTriple.resize(2);
	if (verboseDecode==1) 
		std::cerr << "\n>>>>>symbol is " <<  symbol << " in position " <<  posSymb+1 <<  " of the pattern\n";

	//Initialize triplaFirst to find the first sequence
	//FIRST in position 0
	vectTriple[0].seqN = 0;  //It is not useful
	vectTriple[0].pileN = alpha[int(symbol)];
	vectTriple[0].posN = 1;  //The first occurrence of symbol in F is in the first position in the pile Symbol  
		
	//Initialize triplaLast to find the last sequence
	//LAST in position 1
	vectTriple[1].seqN = 0;  //It is not useful
	vectTriple[1].pileN = alpha[int(symbol)];
	//The last occurrence of the symbol prevSymbol in F is in the last position in the pile prevSymbol 
	//It also corresponds to C[int(symbol) + 1]
	vectTriple[1].posN = 0;
	for (dataTypedimAlpha j = 0 ; j < sizeAlpha; j++) 
			vectTriple[1].posN += tableOcc[vectTriple[1].pileN][j];
	 
	if (verboseDecode==1) {
			std::cerr << "Init triples: "  <<  "\n";
			std::cerr << "Q  ";
			for (dataTypeNSeq g = 0 ; g < 2; g++) {  
				std::cerr << (int)vectTriple[g].pileN << " ";
			}
			std::cerr << std::endl;
			std::cerr << "P  ";
			for (dataTypeNSeq g = 0 ; g < 2; g++) {  
				std::cerr << vectTriple[g].posN  << " ";
			}
			std::cerr << std::endl;
			std::cerr << "N  ";
			for (dataTypeNSeq g = 0 ; g < 2; g++) {  
				std::cerr << vectTriple[g].seqN  << " ";
			}
			std::cerr << std::endl;
		}

	//The new positions of symbol followed by kmer[posSymb] in F is computed by following function 
		
		if (BackByVector == 0) {
			std::cerr << "For the computation of the new positon useful for BackSearch you don't use the vector of the occurrences. You read the file" << std::endl;
		}
		else {
			std::cerr << "For the computation of the new positon useful for BackSearch, it uses a sampling of the occurrences for each segment: " << DIMBLOCK << " size." << std::endl;
		}
		

	while (((vectTriple[0].pileN == vectTriple[1].pileN) && (vectTriple[0].posN < vectTriple[1].posN)) && (posSymb >= 1)) {
		symbol = kmer[posSymb - 1];
		if (verboseDecode==1) 
			std::cerr << "\n>>>>>symbol is " <<  symbol << " in position " <<  posSymb <<  " of the pattern\n";
		
		//The new positions of symbol followed by kmer[posSymb] in F is computed by following function 
		
		int resultCompute = 0;
		if (BackByVector == 0) {
			resultCompute = computeNewPositonForBackSearch (file1, fileOutBwt, symbol);		
		}
		else {
			resultCompute = computeNewPositonForBackSearchByVector (file1, fileOutBwt, symbol);
		}
		checkIfEqual(resultCompute,1);

		if (verboseDecode==1) {
			std::cerr << "New triples: "  <<  "\n";	
			std::cerr << "Q  ";
			for (dataTypeNSeq g = 0 ; g < 2; g++) {  
				std::cerr << (int)vectTriple[g].pileN << " ";
			}
			std::cerr << std::endl;
			std::cerr << "P  ";
			for (dataTypeNSeq g = 0 ; g < 2; g++) {  
				std::cerr << vectTriple[g].posN  << " ";
			}
			std::cerr << std::endl;
			std::cerr << "N  ";
			for (dataTypeNSeq g = 0 ; g < 2; g++) {  
				std::cerr << vectTriple[g].seqN  << " ";
			}
			std::cerr << std::endl;
		}
		posSymb--;
		if (verboseDecode==1)
			std::cerr << ">>>>>Next symbol in position " << posSymb << "\n";
	}
	
	return vectTriple[1].posN - vectTriple[0].posN + 1;
}

int BCRexternalBWT::computeVectorUnbuildBCR(char const* file1, char const* fileOutBwt, dataTypeNChar freq[])
{
	numBlocksInPartialBWT.resize(sizeAlpha);
	for (dataTypedimAlpha x = 0 ; x < sizeAlpha; x++) { 
		numBlocksInPartialBWT[x] = (dataTypeNChar)ceil((long double)freq[alphaInverse[x]]/DIMBLOCK);
		if (verboseDecode==1) 
			std::cerr << "numBlocksInPartialBWT[ " << (int)x << " ]= " << numBlocksInPartialBWT[x] << "\n";
	}


	// Start by allocating an array for array of arrays 
	vectorOcc.resize(sizeAlpha);    //For each BWT-partial 
	// Allocate an array for each element of the first array 
	for (dataTypedimAlpha x = 0 ; x < sizeAlpha; x++) {       //For each block of BWT-partial
		vectorOcc[x].resize(sizeAlpha);					//SumCumulative for each symbol and each block
		// Allocate an array of integers for each element of this symbol     
		for (dataTypedimAlpha y = 0 ; y < sizeAlpha; y++)         //For each block
			vectorOcc[x][y].resize(numBlocksInPartialBWT[x],0);		
	}
		
	char *filenameIn = new char[12];
	char *filename = new char[8];
	const char *ext = "";
	dataTypeNChar numchar=0;
	static FILE *InFileBWT;
	uchar *bufBlock = new uchar[DIMBLOCK];

	
	for (dataTypedimAlpha x = 0 ; x < sizeAlpha; x++) {   //For each BWT-partial
		numchar=sprintf (filename, "%s%d", fileOutBwt, x);
		numchar=sprintf (filenameIn,"%s%s",filename,ext);
		char *newfilename = new char[strlen(file1) + strlen(filenameIn)+1];
		numchar=sprintf (newfilename,"%s%s",file1,filenameIn);
			
		InFileBWT = fopen(newfilename, "rb");
		if (InFileBWT==NULL) {
			std::cerr << "computeVectorUnbuildBCR: could not open file " << newfilename << " !" << std::endl; 
			exit (EXIT_FAILURE);
		}

		dataTypeNChar numBlock = 0;
		while( !feof(InFileBWT) && (numBlock < numBlocksInPartialBWT[x]))   //Added check on numBlocks
		{
			dataTypelenSeq num_read = fread(bufBlock,sizeof(uchar),DIMBLOCK,InFileBWT);
			for (dataTypelenSeq i=0; i<num_read; i++) {
				vectorOcc[x][alpha[(int)(bufBlock[i])]][numBlock]++;
			}
			numBlock++;
		}
		fclose(InFileBWT);
		delete [] newfilename;
		if ( !feof(InFileBWT) && (numBlock > numBlocksInPartialBWT[x])) {
			std::cerr << "computeVectorUnbuildBCR: Error - The file contains more blocks than allocates.\n" ; 
			exit(1);
		}
		//Compute the sum cumulative for each BWT-partial
		for (dataTypedimAlpha z = 0 ; z < sizeAlpha; z++)        //For each symbol z
			for(dataTypeNChar y = 1; y < numBlocksInPartialBWT[x] ; y++)         //For each block y>1 of partial-BWT x
				vectorOcc[x][z][y]=vectorOcc[x][z][y-1] + vectorOcc[x][z][y];   //Sum the previous one: ie Blcok y and block y-1 
	}
	
	delete [] filenameIn;
	delete [] filename;
	delete [] bufBlock;
	/*
	#ifdef DEBUG
		for (dataTypedimAlpha x = 0 ; x < sizeAlpha; x++) {
			std::cerr << "x = " << (int)x << " For the " << alphaInverse[x] << "-BWT-partial: the #symbols is " << freq[alphaInverse[x]] << " Number of block of the symbol " << numBlocksInPartialBWT[x] << "\n";
			for(dataTypedimAlpha z = 0; z < sizeAlpha; ++z) {
				std::cerr << "Symbol: " << (int)z << ":\t";
				for(dataTypeNChar y = 0; y < numBlocksInPartialBWT[x]; ++y)            
					std::cerr << vectorOcc[x][z][y] << "\t";        
				}
				std::cerr << "\n";
			}
			std::cerr << "\n";
		}
	#endif
	*/
	return 1;
}


int BCRexternalBWT::initializeUnbuildBCR(char const* file1, char const* fileOutBwt, dataTypeNChar freq[])
{
	//We supposed that the symbols in the input file are the following
	//TODO
	for (dataTypedimAlpha i = 0; i < 255; i++)
		freq[i]=0;
	freq[int(TERMINATE_CHAR)]=1;
	freq[int('A')]=1;
	freq[int('C')]=1;
	freq[int('G')]=1;
	freq[int('N')]=1;
	freq[int('T')]=1;
	//GIOVANNA: ADDED THE SYMBOL Z IN THE ALPHABET, SO sizeAlpha = alphabetSize
	freq[int('Z')]=1;

	//Compute size of alphabet
	sizeAlpha=0;
	for (dataTypedimAlpha i = 0; i < 255; i++)
		if (freq[i] > 0) 
			sizeAlpha++;

	//Compute alpha and alphaInverse
	alphaInverse = new dataTypedimAlpha[sizeAlpha];
	dataTypedimAlpha mmm=0;
	for (dataTypedimAlpha i = 0; i < 255; i++)
		if (freq[i] > 0) {	
			alpha[i] = mmm;
			alphaInverse[mmm]=i;			
			std::cerr << i << "\t" << freq[i] << "\t" << (int)alpha[i] << "\t" << (int)alphaInverse[mmm] << "\n";
			mmm++;
		}


	std::cerr << "sizeof(type size of alpha): " << sizeof(dataTypedimAlpha) << "\n";
	std::cerr << "sizeof(type of #sequences): " << sizeof(dataTypeNSeq) << "\n";
	std::cerr << "sizeof(type of #characters): " << sizeof(dataTypeNChar) << "\n";

	lengthTot = 0;  //Counts the number of symbols
	nText = 0;
	lengthRead = 0;
	lengthTot_plus_eof = 0;

	tableOcc = new dataTypeNChar*[sizeAlpha]; 
	for (dataTypedimAlpha j = 0 ; j < sizeAlpha; j++) {   //Counting for each pile: $-pile, A-pile, C-pile, G-pile, N-pile, T-pile
			tableOcc[j] = new dataTypeNChar[sizeAlpha];
	  }
	for (dataTypedimAlpha j = 0 ; j < sizeAlpha; j++) 
		for (dataTypedimAlpha h = 0 ; h < sizeAlpha; h++) 
			tableOcc[j][h]=0;

	char *filenameIn = new char[12];
	char *filename = new char[8];
	const char *ext = "";
	dataTypeNChar numchar=0;
	static FILE *InFileBWT;
	uchar *buf = new uchar[SIZEBUFFER];

	for (dataTypedimAlpha g = 0 ; g < sizeAlpha; g++) {  
		numchar=sprintf (filename, "%s%d", fileOutBwt, g);
		numchar=sprintf (filenameIn,"%s%s",filename,ext);
		char *newfilename = new char[strlen(file1) + strlen(filenameIn)+1];
		numchar=sprintf (newfilename,"%s%s",file1,filenameIn);
			
		InFileBWT = fopen(newfilename, "rb");
		if (InFileBWT==NULL) {
			std::cerr << "initializeUnbuildBCR: could not open file " << newfilename << " !" << std::endl; 
			exit (EXIT_FAILURE);
		}

		while( !feof(InFileBWT))
		{
			dataTypelenSeq num_read = fread(buf,sizeof(uchar),SIZEBUFFER,InFileBWT);
			for (dataTypelenSeq i=0; i<num_read; i++) {
				tableOcc[g][alpha[(int)(buf[i])]]++;
			}
			lengthTot_plus_eof += num_read;
		}
		fclose(InFileBWT);
		delete [] newfilename;	
	}
	
	delete [] filenameIn;
	delete [] filename;

	nText = 0;
	for (dataTypedimAlpha g = 0 ; g < sizeAlpha; g++)  
		nText += tableOcc[alpha[int(TERMINATE_CHAR)]][g];
	lengthTot = lengthTot_plus_eof - nText;
	lengthRead = lengthTot / nText;
	std::cerr << "\nNumber of sequences: " << nText << "\n";
	std::cerr << "Length of each sequence: " << lengthRead << "\n\n";
	std::cerr << "Total length (without $): " << lengthTot << "\n";
	std::cerr << "Total length (with $): " << lengthTot_plus_eof << "\n";
	//if (verboseDecode == 1) {
		std::cerr << "TableOcc: "  << "\n";
		for (dataTypedimAlpha g = 0 ; g < sizeAlpha; g++) {
			std::cerr << int(g)  << ":\t";
			for (dataTypedimAlpha j = 0 ; j < sizeAlpha; j++) 
				std::cerr << tableOcc[g][j]  << "\t";
			std::cerr << "\n";	
		}
	//}
	
	for (dataTypedimAlpha j = 0 ; j < 255; j++) 
		freq[j]=0;
	//Compute of the frequency of each symbol
	for (dataTypedimAlpha j = 0 ; j < sizeAlpha; j++) 		
		for (dataTypedimAlpha h = 0 ; h < sizeAlpha; h++)  
			freq[(int)alphaInverse[j]] += tableOcc[j][h];
				
	return 1;
}

int BCRexternalBWT::unbuildBCR(char const* file1, char const* fileOutBwt, char const * fileOut, char const * fileOutput)
{
	dataTypeNChar freq[256];  //contains the distribution of the symbols.
	int resultInit = initializeUnbuildBCR(file1, fileOutBwt, freq);
	checkIfEqual (resultInit,1);

	if (BackByVector == 1) {
	  resultInit = computeVectorUnbuildBCR(file1, fileOutBwt, freq);
	  checkIfEqual(resultInit,1);
	}

	if (decodeBackward == 1) {
		std::cerr << "Inverse BWT by Backward direction." << std::endl;
		decodeBCRmultipleReverse(file1, fileOutBwt, fileOut);
		std::cerr << "The cyc files have been built. Building the sequences." << std::endl;
		TransposeFasta trasp;
		int res = trasp.convertFromCycFileToFasta(fileOutput, nText, lengthRead);
		checkIfEqual (res, 1);
		if (deleteCycFile == 1)  {
			std::cerr << "Removing the auxiliary input file (cyc files)\n";
			char *filename1;
			filename1 = new char[strlen(fileOut)+sizeof(dataTypelenSeq)*8];

			// delete output files
			for(dataTypelenSeq i=0;i<lengthRead;i++ ) {
				sprintf (filename1, "%s%u.txt", fileOut, i);
				if (remove(filename1)!=0) 
					std::cerr << filename1 <<" BCRexternalBWT: Error deleting file" << std::endl;
			}
			delete [] filename1;
		}
	}
	else {
		std::cerr << "Inverse BWT by Forward direction."  << std::endl;
		decodeBCRnaiveForward(file1, fileOutBwt, fileOutput);
	}

	//Free the memory
	for (dataTypedimAlpha j = 0 ; j < sizeAlpha; j++) { 
		delete [] tableOcc[j];
		tableOcc[j] = NULL;
	}
	delete [] tableOcc;
	
	delete[] alphaInverse;

	return true;
}

int BCRexternalBWT::update_Pos_Pile(sortElement *tripla)
{
	//I have to find the position of toFindSymbol in corrected partial BWT
	//To find the pile, where the posN occurrences is, we use tableOcc.
	dataTypeNChar sumOccPileN=0;
	dataTypedimAlpha currentPile=0;
	while ((sumOccPileN < tripla->posN) && (currentPile < sizeAlpha)) {
			sumOccPileN += tableOcc[currentPile][tripla->pileN];
			currentPile++;
	}
	if (sumOccPileN >= tripla->posN) //it means that the pile, where toFindSymbol is, is currentPile-1 and the position 
	{
			currentPile--;
			sumOccPileN = sumOccPileN - tableOcc[currentPile][tripla->pileN];
			tripla->posN = tripla->posN - sumOccPileN;
			tripla->pileN = currentPile;
	}
	else
			std::cerr << "update_Pos_Pile: symbol " << (int)tripla->pileN <<" not found: " << tripla->posN << "occurrence.\n";
	return 1;
}

int BCRexternalBWT::update_Pos_Pile_Blocks(dataTypeNChar *toRead, dataTypeNChar *numBlock, dataTypedimAlpha currentPile, uchar toFindSymbol)
{
	//I have to find the position of toFindSymbol in corrected blocks in the partial BWT
	//To find the block in the pile, where the posN occurrences is, we use vectorOcc.
	/*
	//Linear scanning
	*numBlock = 0;
	while ((vectorOcc[currentPile][alpha[(int)(toFindSymbol)]][*numBlock] < *toRead) && (*numBlock <=numBlocksInPartialBWT[currentPile] ))  {  //Added checks on numBlocks
		(*numBlock)++;
	}
	assert (*numBlock <=numBlocksInPartialBWT[currentPile] );
	*/
	//Binary search for scanning
	vector<dataTypeNChar>::iterator low;

	low=lower_bound (vectorOcc[currentPile][alpha[(int)(toFindSymbol)]].begin(), vectorOcc[currentPile][alpha[(int)(toFindSymbol)]].end(), *toRead); //          ^
	*numBlock = (dataTypeNChar)(low - vectorOcc[currentPile][alpha[(int)(toFindSymbol)]].begin());
	//assert (*numBlock <=numBlocksInPartialBWT[currentPile] );
        if (*numBlock > numBlocksInPartialBWT[currentPile] ){
            cerr << "Numblock size mismatch: " << *numBlock << " < "
                 << numBlocksInPartialBWT[currentPile]
                 << ". Aborting." << endl;
        }
        
	if ((*numBlock) > 0) {
		*toRead = *toRead - vectorOcc[currentPile][alpha[(int)(toFindSymbol)]][*numBlock-1];  //vectorOcc is indexed by 0	
	}

	return 1;
}

vector <int> BCRexternalBWT::recoverNSequenceForward(char const* file1, char const * fileOutBwt, dataTypeNSeq numKmersInput)
{
	sortElement tripla;
	dataTypeNSeq numTotKmers=0;
	for (dataTypeNSeq g = 0 ; g < numKmersInput; g++) {
		std::cerr << "Initialization for the k-mer of index " << LastVector[g].seqN << ". Number of occurrences " << LastVector[g].posN-FirstVector[g].posN+1 << std::endl;
		//Initialize triple
		for (dataTypeNChar  j = FirstVector[g].posN ; j <= LastVector[g].posN; j++) { //For each position between first and last
			tripla.seqN = numTotKmers;
			tripla.posN = j;  
			tripla.pileN = FirstVector[g].pileN;
			vectTriple.push_back(tripla);
			numTotKmers++;
		}
	}
	std::cerr << "We want to compute the seqID of " << numTotKmers  << " sequeces." << std::endl;

	quickSort(vectTriple);
	uchar *toFindSymbols = new uchar[numTotKmers];  //Symbol to find for each kmers
	dataTypeNSeq h;
	for ( h = 0 ; h < numTotKmers; h++) 
		toFindSymbols[h] = alphaInverse[vectTriple[h].pileN];  //The first time vectTriple[h].seqN == h
	
	h = 0 ; 
	bool existDollars = false;  //We suppose that  there are not TERMINATE_CHAR
	while ((h < numTotKmers) && (existDollars != true)) {
		if (toFindSymbols[h] != TERMINATE_CHAR) 
			existDollars = true;  //There are at least 1 symbol TERMINATE_CHAR	
		h++;
	}

	int result = 0;
	dataTypeNSeq countDollars = 0;
	while (existDollars == true) {   //If there is at least one TERMINATE_CHAR symbol
		//cerr << "another round existDollars" << endl;
		//Update the PileN where the number of occurrences is, for each h
		//posN is the number of occurrences (absolute value) in the entire BWT
		for ( h = 0 ; h < numTotKmers; h++) {
			if (toFindSymbols[vectTriple[h].seqN] != TERMINATE_CHAR) {   //if =TERMINATE_CHAR, it means that we have already obtained the seqID 
				//cerr << "calling update_Pos_Pile" << endl;
				result = update_Pos_Pile(&vectTriple[vectTriple[h].seqN]); //posN is the number of occurrences (relative value) in the pileN-BWT-partial
				checkIfEqual(result,1);
			}
		}
		//Compute the rank inverse and inserts the number of read symbols into posN. Update posN
		//cerr << "calling rankInverseManyByVector()";
		result = rankInverseManyByVector (file1, fileOutBwt, numTotKmers, toFindSymbols);
		//cerr << "done." << endl;
		checkIfEqual(result,1);
		/*
		dataTypeNChar readChar = 0; 
		for ( h = 0 ; h < numTotKmers; h++) {
			if (toFindSymbols[h] != TERMINATE_CHAR) {
				readChar=findRankInBWTbyVector (file1, fileOutBwt, vectTriple[h].pileN, vectTriple[h].posN, toFindSymbols[h]);
				assert(readChar!=0);
				vectTriple[h].posN = readChar;
			}
		}
		*/
		quickSort(vectTriple);
		//Update toFindSymbol and count the dollars
		countDollars = 0;
		for ( h = 0 ; h < numTotKmers; h++) {
			if (toFindSymbols[h] != TERMINATE_CHAR) {
				toFindSymbols[h] = alphaInverse[vectTriple[h].pileN];
			}
			else
				countDollars++;
		}
		//std::cerr << "countDollars " << countDollars  << " ." << std::endl;

		if (countDollars >= numTotKmers) {
			existDollars = false; //The end!
		}
	}

	vector <int> seqID;
	seqID.resize(numTotKmers);
	//The position is indexed by 1, the number of sequence by 0
	for ( h = 0 ; h < numTotKmers; h++) {
		seqID[vectTriple[h].seqN] = vectTriple[h].posN - 1;
	}
	vectTriple.clear();  //Erase all elements of vector.
	delete[] toFindSymbols;
	return seqID;
}

int BCRexternalBWT::recoverNSequenceForwardSequentially(char const* file1, char const * fileOutBwt, dataTypeNSeq numKmersInput) {
		//Compute the seqID sequentially

		//Now, you must find seqN for each position between vectTriple[0].posN and vectTriple[1].posN of the BWT-partial vectTriple[0].pileN=vectTriple[1].posN
		
		for (dataTypeNSeq g = 0 ; g < numKmersInput; g++) { 
				//Recover the number of the sequence seqN of the kmer one symbol at time in reverse order
				uchar *sequence = new uchar[lengthRead + 2];
				sortElement tripla;
				std::cerr << "List of the seqID containing the k-mer with index" << g << ":" <<std::endl;
				
				for (dataTypeNChar  j = FirstVector[g].posN ; j <= LastVector[g].posN; j++) { //For each position between first and last
					for (dataTypelenSeq mmm = lengthRead+2; mmm > 0; mmm--) 
						sequence[mmm-1] = '\0';
					//Initialize tripla to find the sequence
					tripla.seqN = g;
					tripla.posN = j;  
					tripla.pileN = FirstVector[g].pileN;
					dataTypelenSeq lenSeq = 0;
					
					//#ifdef DEBUG
					//	std::cerr << "Starting Tripla for the suffix: \tQ= " << (int)tripla.pileN << " P= " << tripla.posN  << " N= " << tripla.seqN  << std::endl;
					//#endif

					dataTypeNSeq numberOfSeq = recover1SequenceForward(file1, fileOutBwt, tripla, sequence, &lenSeq);
					//#ifdef DEBUG
					//	std::cerr << " Computed suffix is " << sequence << "! It is long  " << lenSeq << ". It belongs to " << numberOfSeq << " sequence of the collection" << std::endl;
					//#endif
					
					std::cerr << "pos in the SA=\t"<< j << "\t SeqId=\t" << numberOfSeq <<  std::endl;
			
				}
				delete [] sequence;
		}
	return 1;
}

//Reconstruct 1 sequence backwards by threading through the LF-mapping and reading the characters off of F column.
dataTypeNSeq BCRexternalBWT::recover1SequenceForward(char const* file1, char const * fileOutBwt, sortElement tripla, uchar *sequence, dataTypelenSeq *lenCheck)
{
	//The toFindSymbol is into F column, it is in pileN-BWT in the position posN. So, it is the posN occurrences of alphaInverse[pileN] in F.
	//So, toFindSymbol is the alphaInverse[pileN]

	*lenCheck = 0;
	uchar toFindSymbol = alphaInverse[tripla.pileN];
	//dataTypeNChar rankFoundSymbol;
	
//	if (verboseDecode == 1) { 
//		std::cerr << "The symbol is: " << toFindSymbol << "\n";
//		std::cerr << "\nI have to find the position of the " << tripla.posN << " " << toFindSymbol << " in the whole BWT\n"; 
//	}
	sequence[0] = toFindSymbol;
	//dataTypeNChar numcharWrite = fwrite (&toFindSymbol, sizeof(uchar), 1 , InfileOutDecode); 
	//assert( numcharWrite == 1); // we should always read the same number of characters
	(*lenCheck)++;
	while ((toFindSymbol != TERMINATE_CHAR) && (*lenCheck <= lengthRead)) {
			
		//posN is the number of occurrences (absolute value) in the entire BWT
		int result = update_Pos_Pile(&tripla); //posN is the number of occurrences (relative value) in the pileN-BWT-partial
		checkIfEqual(result,1);
				
	//		if (verboseDecode == 1) 
	//		std::cerr << "I have to find the position of the " << rankFoundSymbol << " occurrences of the symbol " <<  toFindSymbol << " in " << (int)tripla.pileN << " pile \n";
		//I have to read the pileN until I find rankFoundSymbol symbols. The found value is posN, i.e. the position of the next symbol
		
		dataTypeNChar readChar = 0; 
		if (BackByVector == 0) {
			readChar=findRankInBWT (file1, fileOutBwt, tripla.pileN, tripla.posN, toFindSymbol);		
		}
		else {
			readChar=findRankInBWTbyVector (file1, fileOutBwt, tripla.pileN, tripla.posN, toFindSymbol);
		}
		checkIfNotEqual(readChar,0);

		tripla.posN = readChar;
//		if (verboseDecode == 1) 
//				std::cerr << "The occurrence " << rankFoundSymbol << " of the symbol " << toFindSymbol << " is in position " << tripla.posN << "\n\n";

		toFindSymbol = alphaInverse[tripla.pileN];
		sequence[*lenCheck] = toFindSymbol;
//		if (verboseDecode == 1) { 
//				std::cerr << "The symbol is: " << toFindSymbol << "\n";
//				std::cerr << "I have to find the position of the " << tripla.posN << "  " << toFindSymbol << " in the whole BWT\n";
//		}
		(*lenCheck)++;
	}
	//if (verboseDecode == 1) 
	//	std::cerr << lenCheck << " " << lengthRead << "\n";

	//if (verboseDecode==1) {
	//	std::cerr << "***********Found the $-sign in First column \t";
	//	std::cerr << "Q= " << (int)tripla.pileN << " P= " << tripla.posN  << " N= " << tripla.seqN  << std::endl;
	//}

	//The position is indexed by 1, the number of sequence by 0
	return tripla.posN - 1;
}

//Inverse BWT by Forward direction of nText sequences, one sequence at a time, in lexicographic order.
//Reconstruct the sequences one at a time in forward order
//file1 is the input file
//fileOutBwt is the suffix of the auxiliary files for the partial BWTs
//fileOutDecode is the output, that is the texts
int BCRexternalBWT::decodeBCRnaiveForward(char const* file1, char const * fileOutBwt, char const * fileOutDecode)
{		
	dataTypeNChar numchar=0;
	
	const char *fileEndPos="outFileEndPos.bwt";
	static FILE *InFileEndPos;                  // input file of the end positions;
	InFileEndPos = fopen(fileEndPos, "rb");
	if (InFileEndPos==NULL) {
			std::cerr << "decodeBCRnaiveForward: could not open file " << fileEndPos << " !" << std::endl; 
			exit (EXIT_FAILURE);
	}

	static FILE *InfileOutDecode;                  // output Decode file;
	InfileOutDecode = fopen(fileOutDecode, "wb");
	if (InfileOutDecode==NULL) {
			std::cerr << "decodeBCRnaiveForward: could not open file " << fileOutDecode << " !" << std::endl; 
			exit (EXIT_FAILURE);
	}
	dataTypeNSeq numText = 0;
	numchar = fread (&numText, sizeof(dataTypeNChar), 1 , InFileEndPos);
	checkIfEqual (numchar,1);
	checkIfEqual (nText,numText); // we should always read the same number of Texts of the bwt

	numchar=0;
	sortElement triple;
	std::cerr << "Recover the sequences of the collection in lexicographic order. A sequence at a time!" << std::endl; 
	if (BackByVector == 0) {
		std::cerr << "It is not using the sampling of the BWT. It requires more time!"<< std::endl;
	}
	else {
		std::cerr << "It is using the sampling of the BWT. It requires more memory!"<< std::endl;
		std::cerr << "In order to do this, it uses a sampling of the occurrences for each segment: " << DIMBLOCK << " size." << std::endl;
	}
	for (dataTypeNSeq i = 0; i < nText; i++) {
		numchar = fread (&triple.seqN, sizeof(dataTypeNSeq), 1 , InFileEndPos); 
		checkIfEqual( numchar, 1); // we should always read the same number of characters
		numchar = fread (&triple.posN, sizeof(dataTypeNChar), 1 , InFileEndPos);    //it is the relative position of the $ in the partial BWT 
		checkIfEqual(numchar,1); // we should always read the same number of characters
		numchar = fread (&triple.pileN, sizeof(dataTypedimAlpha), 1 , InFileEndPos);    
		checkIfEqual(numchar,1); // we should always read the same number of characters
	
		//		if (verboseDecode == 1)
		//			std::cerr << std::endl << "Starting Tripla: " << triple.seqN << " " << triple.posN << " " << (int)triple.pileN << "\n" << std::endl;
	
		uchar *sequence = new uchar[lengthRead + 2];
		for (dataTypelenSeq j = lengthRead+2; j > 0; j--) 
			sequence[j-1] = '\0';
		
		dataTypelenSeq lenSeq = 0;
		dataTypeNSeq numberOfSeq = recover1SequenceForward(file1, fileOutBwt, triple, sequence, &lenSeq);
		checkIfEqual(numberOfSeq,triple.seqN);

		//		std::cerr << "The " << i+1 <<"-th/" << nText <<" computed sequence is " << sequence << "! It is long  " << lenSeq << ". It belongs to " << numberOfSeq << " sequence of the collection" << std::endl;
		if (verboseDecode==1) 
			cerr << numberOfSeq << "\t" << sequence << endl;

		dataTypelenSeq numcharWrite = 0;
		numcharWrite = fwrite (sequence, sizeof(uchar), lenSeq , InfileOutDecode); 
                checkIfEqual( numcharWrite ,lenSeq); // we should always read the same number of characters
		numcharWrite = fwrite ("\n", sizeof(char), 1 , InfileOutDecode); 
		checkIfEqual( numcharWrite,1); // we should always read the same number of characters
		delete [] sequence;	
	}

	fclose(InFileEndPos);		
	fclose(InfileOutDecode);
	return true;
}

//Multiple Decoding the sequences (Build reverse sequence) 
//Reconstruct m sequences backwards by threading through the FL-mapping and reading the characters off of L.
//file1 is the input file
//fileOutBWT is the suffix of the filename of the partial BWTs
//fileOut is the prefix of the lengthRead-filename (traspose texts: cyc.i.txt)
//Inverse BWT by Backward direction of nText sequences at the same time by lengthRead iterations.
int BCRexternalBWT::decodeBCRmultipleReverse(char const* file1, char const* fileOutBwt, char const * fileOut)
{		
	vectTriple.resize(nText);

	//As I want to compute the reverse sequences, I need the position of $ in F
	for (dataTypeNSeq g = 0 ; g < nText; g++) {  
		vectTriple[g].pileN = alpha[int(TERMINATE_CHAR)];   //So the 0-pile
		vectTriple[g].posN = g + 1;  
		vectTriple[g].seqN = g;
	}

	if (verboseDecode == 1) {
		std::cerr << "The Initial triples of $ in first column are!"<< std::endl;
		std::cerr << "Q  ";
		for (dataTypeNSeq g = 0 ; g < nText; g++) {  
			std::cerr << (int)vectTriple[g].pileN << " ";
		}
		std::cerr << std::endl;
		std::cerr << "P  ";
		for (dataTypeNSeq g = 0 ; g < nText; g++) {  
			std::cerr << vectTriple[g].posN  << " ";
		}
		std::cerr << std::endl;
		std::cerr << "N  ";
		for (dataTypeNSeq g = 0 ; g < nText; g++) {  
			std::cerr << vectTriple[g].seqN  << " ";
		}
		std::cerr << std::endl;
	}
	
	static FILE *InfileOutDecodeCyc;                 
	uchar *newSymb = new uchar[nText];
	char *filename = new char[strlen(fileOut)+sizeof(dataTypelenSeq)*8+1];
	
	//As we recover the symbol in reverse order, I store the first found symbol in cyc.(length-1).txt file
	//and the last found symbol in cyc.0.txt file 
	if (BackByVector == 0) {
		std::cerr << "It is not using the sampling of the BWT. It requires more time!"<< std::endl;
	}
	else {	
		std::cerr << "It is using the sampling of the BWT. It requires more memory!"<< std::endl;
		std::cerr << "In order to do this, it uses a sampling of the occurrences for each segment: " << DIMBLOCK << " size." << std::endl;
	}
	for (dataTypelenSeq m = lengthRead ; m > 0 ; m--) {      

		int resultNsymbol = -1;
		if (BackByVector == 0) {
			resultNsymbol = RecoverNsymbolsReverse (file1, fileOutBwt, newSymb);
		}
		else {	
			resultNsymbol =RecoverNsymbolsReverseByVector(file1, fileOutBwt, newSymb);
		}
		checkIfEqual (resultNsymbol ,1);

		sprintf (filename, "%s%u.txt", fileOut, m-1);
		InfileOutDecodeCyc = fopen(filename, "wb");
		if (InfileOutDecodeCyc==NULL) {
			std::cerr << "decodeBCRmultipleReverse: could not open file " << filename << " !" << std::endl; 
			exit (EXIT_FAILURE);
		}

		dataTypeNChar numcharWrite = fwrite (newSymb, sizeof(uchar), nText , InfileOutDecodeCyc); 
		checkIfEqual( numcharWrite,nText); // we should always read the same number of characters

		fclose(InfileOutDecodeCyc);
	}
	delete[] filename;
	delete [] newSymb;
	
	return true;
}

//It is used to reconstruct m sequences backwards by threading through the FL-mapping and reading the characters off of L.
int BCRexternalBWT::RecoverNsymbolsReverse(char const* file1, char const* fileOutBwt, uchar * newSymb)
{
	dataTypeNChar numchar=0;
	char *filenameIn = new char[12];
	char *filename = new char[8];
	const char *ext = "";
	numchar=0;
	static FILE *InFileBWT;
	
	dataTypeNChar toRead = 0;
	dataTypeNChar *counters = new dataTypeNChar[sizeAlpha];  //it counts the number of each symbol into the i-Pile-BWT
	dataTypeNSeq j = 0;
	while (j < nText) {			
		for (dataTypedimAlpha i = 0 ; i < sizeAlpha; i++){
			counters[i]=0;
		}
		dataTypedimAlpha currentPile = vectTriple[j].pileN;
		numchar=sprintf (filename, "%s%d", fileOutBwt, currentPile);
		numchar=sprintf (filenameIn,"%s%s",filename,ext);
		//if (verboseDecode == 1)
		//	std::cerr << "===Current BWT-partial= " << (int)currentPile << "\n";
		
		char *newfilename = new char[strlen(file1) + strlen(filenameIn)+1];
		numchar=sprintf (newfilename,"%s%s",file1,filenameIn);
		InFileBWT = fopen(newfilename, "rb");
		if (InFileBWT==NULL) {
			std::cerr << "RecoverNsymbolsReverse: BWT file " << (int)j << ": Error opening " << std::endl;
			exit (EXIT_FAILURE);
		}
		dataTypeNSeq k=j;
		dataTypeNChar cont = 0;   //number of the read symbols
		uchar foundSymbol;
		//dataTypelenSeq lenCheck=0;
		dataTypeNChar numberRead=0;
		while ((k< nText) && (vectTriple[k].pileN == currentPile)) {
			if (verboseDecode == 1) {
				std::cerr << "Sequence number " << k << "\n";
				std::cerr << "j-1: Q["<<k<<"]=" << (int)vectTriple[k].pileN << " P["<<k<<"]=" << (dataTypeNChar)vectTriple[k].posN << " N["<<k<<"]=" << (dataTypeNSeq)vectTriple[k].seqN << "\n";
			}
			//The symbol for the sequences seqN in F[posN]  is the symbol
			//symbol = alphaInverse[vectTriple[k].pileN];
			//Now, I have to find the new symbol, it is in B[pileN] in position posN and so I can update pileN and posN
			
			//For any character (of differents sequences) in the same pile
			foundSymbol = '\0';
			//cont is the number of symbols already read!
			toRead = vectTriple[k].posN - cont;

			numberRead = rankManySymbols(*InFileBWT, counters, toRead, &foundSymbol);

			if( verboseDecode==1 )
			  {
			    std::cerr << "toRead " << toRead << " Found Symbol is " << foundSymbol << "\n";
			  }
			checkIfEqual (toRead,numberRead);
			cont += numberRead;
						
			//I have to update the value in vectTriple[k].posN, it must contain the position of the symbol in F
			//Symbol is
			if (verboseDecode == 1)
				std::cerr << "vectTriple[k].seqN = " << vectTriple[k].seqN << " Symbol = " << foundSymbol << "\n";
			newSymb[vectTriple[k].seqN] = foundSymbol;
			//PosN is
			vectTriple[k].posN = counters[alpha[(int)foundSymbol]];   
			//if (verboseDecode == 1)				
			//	std::cerr << "\nCompute PosN\nInit New P["<< k <<"]= " << vectTriple[k].posN <<std::endl;
			for (dataTypedimAlpha g = 0 ; g < currentPile; g++) {  //I have to count in each pile g= 0... (currentPile-1)-pile
				vectTriple[k].posN = vectTriple[k].posN + tableOcc[g][alpha[(int)foundSymbol]];
				//if (verboseDecode == 1) {				
				//	std::cerr << "g= " << (int)g << " symbol= " << (int)symbol << " alpha[symbol]= "<< (int)alpha[(int)symbol] <<std::endl;
				//	std::cerr << "Add New posN[k]=" << vectTriple[k].posN << " tableOcc[g][alpha[(int)symbol]] " << tableOcc[g][alpha[(int)symbol]] <<std::endl;
				//}
			}
			//pileN is
			//std::cerr << "\nCompute Pile\n";
			vectTriple[k].pileN=alpha[(int)foundSymbol];
			if (verboseDecode == 1)
				std::cerr << "Result: j  : Q[q]=" << (int)vectTriple[k].pileN << " P[q]=" << (dataTypeNChar)vectTriple[k].posN <<  " N[q]=" << (dataTypeNSeq)vectTriple[k].seqN << std::endl << std::endl;

			k++;
		}
		fclose(InFileBWT);
		j=k;
		delete [] newfilename;
	}
	delete [] counters;
	delete [] filenameIn;
	delete [] filename;

	if (verboseDecode==1) {
		std::cerr << "NewSymbols " ;
		for (dataTypeNSeq g = 0 ; g < nText; g++) {  
			std::cerr << newSymb[g] << " ";
		}
		std::cerr << std::endl;
		std::cerr << "Before Sorting" << std::endl;
		std::cerr << "Q  ";
		for (dataTypeNSeq g = 0 ; g < nText; g++) {  
			std::cerr << (int)vectTriple[g].pileN << " ";
		}
		std::cerr << std::endl;
		std::cerr << "P  ";
		for (dataTypeNSeq g = 0 ; g < nText; g++) {  
			std::cerr << vectTriple[g].posN  << " ";
		}
		std::cerr << std::endl;
		std::cerr << "N  ";
		for (dataTypeNSeq g = 0 ; g < nText; g++) {  
			std::cerr << vectTriple[g].seqN  << " ";
		}
		std::cerr << std::endl;
	}
	
	quickSort(vectTriple);
		
	if (verboseDecode==1) {
		std::cerr << "After Sorting" << std::endl;
		std::cerr << "Q  ";
		for (dataTypeNSeq g = 0 ; g < nText; g++) {  
			std::cerr << (int)vectTriple[g].pileN << " ";
		}
		std::cerr << std::endl;
		std::cerr << "P  ";
		for (dataTypeNSeq g = 0 ; g < nText; g++) {  
			std::cerr << vectTriple[g].posN  << " ";
		}
		std::cerr << std::endl;
		std::cerr << "N  ";
		for (dataTypeNSeq g = 0 ; g < nText; g++) {  
			std::cerr << vectTriple[g].seqN  << " ";
		}	
		std::cerr << std::endl;
	}

	return 1;
}

//It is used to reconstruct m sequences backwards by threading through the FL-mapping and reading the characters off of L.
int BCRexternalBWT::RecoverNsymbolsReverseByVector(char const* file1, char const* fileOutBwt, uchar * newSymb)
{
	dataTypeNChar numchar=0;
	char *filenameIn = new char[12];
	char *filename = new char[8];
	const char *ext = "";
	numchar=0;
	static FILE *InFileBWT;
	
	dataTypeNChar toRead = 0;
	dataTypeNChar *counters = new dataTypeNChar[sizeAlpha];  //it counts the number of each symbol into the i-Pile-BWT
	dataTypeNSeq j = 0;
	while (j < nText) {			
		for (dataTypedimAlpha i = 0 ; i < sizeAlpha; i++)
			counters[i]=0;

		dataTypedimAlpha currentPile = vectTriple[j].pileN;
		numchar=sprintf (filename, "%s%d", fileOutBwt, currentPile);
		numchar=sprintf (filenameIn,"%s%s",filename,ext);
		//if (verboseDecode == 1)
		//	std::cerr << "===Current BWT-partial= " << (int)currentPile << "\n";
		
		char *newfilename = new char[strlen(file1) + strlen(filenameIn)+1];
		numchar=sprintf (newfilename,"%s%s",file1,filenameIn);
		InFileBWT = fopen(newfilename, "rb");
		if (InFileBWT==NULL) {
			std::cerr << "RecoverNsymbolsReverseByVector: BWT file " << (int)j << ": Error opening " << std::endl;
			exit (EXIT_FAILURE);
		}
		dataTypeNSeq k=j;
		//dataTypeNChar cont = 0;   //number of the read symbols
		uchar foundSymbol;
		//dataTypelenSeq lenCheck=0;
		dataTypeNChar numberRead=0;
		dataTypeNChar numBlock = 0;
		while ((k< nText) && (vectTriple[k].pileN == currentPile)) {
			if (verboseDecode == 1) {
				std::cerr << "Sequence number " << k << "\n";
				std::cerr << "j-1: Q["<<k<<"]=" << (int)vectTriple[k].pileN << " P["<<k<<"]=" << (dataTypeNChar)vectTriple[k].posN << " N["<<k<<"]=" << (dataTypeNSeq)vectTriple[k].seqN << "\n";
			}
			//The symbol for the sequences seqN in F[posN]  is the symbol
			//symbol = alphaInverse[vectTriple[k].pileN];
			//Now, I have to find the new symbol, it is in B[pileN] in position posN and so I can update pileN and posN
			
			//For any character (of differents sequences) in the same pile
			foundSymbol = '\0';
			//cont is the number of symbols already read!
			//toRead = vectTriple[k].posN - cont;
			toRead = vectTriple[k].posN;
			for (dataTypedimAlpha i = 0 ; i < sizeAlpha; i++)
					counters[i]=0;	
			//std::cerr << "toRead is " << toRead << "\n";
			if (toRead > 0) {
				//we need to know how many occurrences of each symbol there are up to the position toRead.
				//if ToRead > dimBlock, we can use vectorOcc in order to find the occorrences in the blocks precede the block where the position toRead is.
				//Before, we need to find the block where toRead position is.
				int result = findBlockToRead(counters, currentPile, &toRead, &numBlock);
				checkIfEqual (result , 1);
				//std::cerr << "numBlock: " << numBlock << " toRead " << toRead << "\n";
			}

			if (toRead <= DIMBLOCK) {   //If toRead == DIMBLOCK, because I can need to known foundSymbol character
				//std::cerr << "Move file to the position " << numBlock*DIMBLOCK <<  "\n";
				fseek (InFileBWT, numBlock*DIMBLOCK, 0);      
				numberRead = rankManySymbolsByVector(*InFileBWT, counters, toRead, &foundSymbol);
				//std::cerr << "foundSymbol " << (int)foundSymbol <<  "\n";
				checkIfEqual (toRead , numberRead);
				//cont += numberRead;
			}
			/*
				std::cerr << "counters  after FirstVector:\t";
				for (dataTypedimAlpha i = 0 ; i < sizeAlpha; i++)
			       std::cerr << " " << counters[i];
				std::cerr << "\n";	
			*/
			//numberRead = rankManySymbols(*InFileBWT, counters, toRead, &foundSymbol);
			
			//std::cerr << "toRead " << toRead << " Found Symbol is " << foundSymbol << "\n";
			//assert (toRead == numberRead);
			//cont += numberRead;
						
			//I have to update the value in vectTriple[k].posN, it must contain the position of the symbol in F
			//Symbol is
			if (verboseDecode == 1)
				std::cerr << "vectTriple[k].seqN = " << vectTriple[k].seqN << " Symbol = " << foundSymbol << "\n";
			
			newSymb[vectTriple[k].seqN] = foundSymbol;

			//PosN is
			vectTriple[k].posN = counters[alpha[(int)foundSymbol]];   

			//if (verboseDecode == 1)				
			//	std::cerr << "\nCompute PosN\nInit New P["<< k <<"]= " << vectTriple[k].posN <<std::endl;
			for (dataTypedimAlpha g = 0 ; g < currentPile; g++) {  //I have to count in each pile g= 0... (currentPile-1)-pile
				vectTriple[k].posN = vectTriple[k].posN + tableOcc[g][alpha[(int)foundSymbol]];
				//if (verboseDecode == 1) {				
				//	std::cerr << "g= " << (int)g << " symbol= " << (int)symbol << " alpha[symbol]= "<< (int)alpha[(int)symbol] <<std::endl;
				//	std::cerr << "Add New posN[k]=" << vectTriple[k].posN << " tableOcc[g][alpha[(int)symbol]] " << tableOcc[g][alpha[(int)symbol]] <<std::endl;
				//}
			}
			//pileN is
			//std::cerr << "\nCompute Pile\n";
			vectTriple[k].pileN=alpha[(int)foundSymbol];
			if (verboseDecode == 1)
				std::cerr << "Result: j  : Q[q]=" << (int)vectTriple[k].pileN << " P[q]=" << (dataTypeNChar)vectTriple[k].posN <<  " N[q]=" << (dataTypeNSeq)vectTriple[k].seqN << std::endl << std::endl;

			k++;
		}
		fclose(InFileBWT);
		j=k;
		delete [] newfilename;
	}
	delete [] counters;
	delete [] filenameIn;
	delete [] filename;

	if (verboseDecode==1) {
		std::cerr << "NewSymbols " ;
		for (dataTypeNSeq g = 0 ; g < nText; g++) {  
		  std::cerr << (char)newSymb[g] << " ";
		}
		std::cerr << std::endl;
		std::cerr << "Before Sorting" << std::endl;
		std::cerr << "Q  ";
		for (dataTypeNSeq g = 0 ; g < nText; g++) {  
			std::cerr << (int)vectTriple[g].pileN << " ";
		}
		std::cerr << std::endl;
		std::cerr << "P  ";
		for (dataTypeNSeq g = 0 ; g < nText; g++) {  
			std::cerr << vectTriple[g].posN  << " ";
		}
		std::cerr << std::endl;
		std::cerr << "N  ";
		for (dataTypeNSeq g = 0 ; g < nText; g++) {  
			std::cerr << vectTriple[g].seqN  << " ";
		}
		std::cerr << std::endl;
	}
	
	quickSort(vectTriple);
		
	if (verboseDecode==1) {
		std::cerr << "After Sorting" << std::endl;
		std::cerr << "Q  ";
		for (dataTypeNSeq g = 0 ; g < nText; g++) {  
			std::cerr << (int)vectTriple[g].pileN << " ";
		}
		std::cerr << std::endl;
		std::cerr << "P  ";
		for (dataTypeNSeq g = 0 ; g < nText; g++) {  
			std::cerr << vectTriple[g].posN  << " ";
		}
		std::cerr << std::endl;
		std::cerr << "N  ";
		for (dataTypeNSeq g = 0 ; g < nText; g++) {  
			std::cerr << vectTriple[g].seqN  << " ";
		}	
		std::cerr << std::endl;
	}

	return 1;
}


//It is used to reconstruct 1 sequences backwards by threading through the FL-mapping and reading the characters off of L.
int BCRexternalBWT::Recover1symbolReverse(char const* file1, char const* fileOutBwt, uchar *newSymbol, sortElement *tripla)
{
	dataTypeNChar numchar=0;
	char *filenameIn = new char[12];
	char *filename = new char[8];
	const char *ext = "";
	numchar=0;
	static FILE *InFileBWT;
	
	dataTypeNChar toRead = 0;
	dataTypeNChar *counters = new dataTypeNChar[sizeAlpha];  //it counts the number of each symbol into the i-Pile-BWT
	for (dataTypedimAlpha i = 0 ; i < sizeAlpha; i++)
		counters[i]=0;

	dataTypedimAlpha currentPile = tripla->pileN;
	numchar=sprintf (filename, "%s%d", fileOutBwt, currentPile);
	numchar=sprintf (filenameIn,"%s%s",filename,ext);
	//if (verboseDecode == 1)
	//	std::cerr << "===Current BWT-partial= " << (int)currentPile << "\n";
		
	char *newfilename = new char[strlen(file1) + strlen(filenameIn)+1];
	numchar=sprintf (newfilename,"%s%s",file1,filenameIn);
	InFileBWT = fopen(newfilename, "rb");
	if (InFileBWT==NULL) {
		std::cerr << "Recover1symbolReverse: BWT file " << (int)currentPile << ": Error opening " << std::endl;
		exit (EXIT_FAILURE);
	}
	dataTypeNChar cont = 0;   //number of the read symbols
	uchar foundSymbol;
	//if (verboseDecode == 1) {
	//	std::cerr << "j-1: Q["<<(int)currentPile<<"]=" << (int)tripla->pileN << " P["<<(int)currentPile<<"]=" << (dataTypeNChar)tripla->posN << " N["<<(int)currentPile<<"]=" << (dataTypeNSeq)tripla->seqN << "\n";
	//}
	//The symbol for the sequences seqN in F[posN]  is the symbol
	//symbol = alphaInverse[vectTriple[k].pileN];
	//Now, I have to find the new symbol, it is in B[pileN] in position posN and so I can update pileN and posN
			
	//For any character (of differents sequences) in the same pile
	foundSymbol = '\0';
	//cont is the number of symbols already read!
	toRead = tripla->posN - cont;

	dataTypeNChar numberRead=0;
	numberRead = rankManySymbols(*InFileBWT, counters, toRead, &foundSymbol);
	//std::cerr << "toRead " << toRead << "Found Symbol is " << foundSymbol << "\n";
	checkIfEqual (toRead ,numberRead);
	cont += numberRead;

	//I have to update the value in tripla.posN, it must contain the position of the symbol in F
	//Symbol is
	//if (verboseDecode == 1)
	//	std::cerr << "tripla.seqN = " << tripla->seqN << " Symbol = " << foundSymbol << "\n";
	*newSymbol = foundSymbol;
	//PosN is
	tripla->posN = counters[alpha[(int)foundSymbol]];   

	//if (verboseDecode == 1)				
	//	std::cerr << "\nCompute PosN\nInit New P= " << (dataTypeNChar)tripla->posN <<std::endl;
	for (dataTypedimAlpha g = 0 ; g < currentPile; g++) {  //I have to count in each pile g= 0... (currentPile-1)-pile
		tripla->posN = tripla->posN + tableOcc[g][alpha[(int)foundSymbol]];
				//if (verboseDecode == 1) {				
				//	std::cerr << "g= " << (int)g << " symbol= " << (int)foundSymbol << " alpha[symbol]= "<< (int)alpha[(int)symbol] <<std::endl;
				//	std::cerr << "Add New posN[k]=" << vectTriple[k].posN << " tableOcc[g][alpha[(int)foundSymbol]] " << tableOcc[g][alpha[(int)foundSymbol]] <<std::endl;
				//}
	}
			//pileN is
			//std::cerr << "\nCompute Pile\n";
	tripla->pileN=alpha[(int)foundSymbol];
	
	//if (verboseDecode == 1)
	//	std::cerr << "Result: j  : Q[q]=" << (int)tripla->pileN << " P[q]=" << (dataTypeNChar)tripla->posN <<  " N[q]=" << (dataTypeNSeq)tripla->seqN << std::endl << std::endl;

	fclose(InFileBWT);
	delete [] newfilename;

	delete [] counters;
	delete [] filenameIn;
	delete [] filename;
	/*
	if (verboseDecode==1) {
		std::cerr << "NewSymbols " ;
		std::cerr << *newSymbol << " ";	
		std::cerr << std::endl;
		std::cerr << "Q  ";
		std::cerr << (int)tripla->pileN << " ";
		std::cerr << std::endl;
		std::cerr << "P  ";
		std::cerr << tripla->posN  << " ";
		std::cerr << std::endl;
		std::cerr << "N  ";
		std::cerr << tripla->seqN  << " ";
		std::cerr << std::endl;
	}
	*/
	return true;
}


BCRexternalBWT::~BCRexternalBWT() 
{
}

