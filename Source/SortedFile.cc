#include "TwoWayList.h"
#include "Record.h"
#include "Schema.h"
#include "File.h"
#include "Comparison.h"
#include "ComparisonEngine.h"
#include "DBFile.h"
#include "Defs.h"
#include <fstream>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

SortedFile::SortedFile () {
	inPipe = new Pipe(100);	
	outPipe = new Pipe(100);	
	bq = NULL;
	file = new File();
	isDirty=0;
	si = NULL;
	current = new Record();	
	readPageBuffer = new Page();		// store contents of current page from where program is reading
	tobeMerged = new Page();			// used for merging existing records with those from BigQ
	m = R;								// Mode set to read
	OrderMaker *queryOrder = NULL;		// queryOrder used in GetNext(3 parameters)
	bool queryChange = true;			// maintains if query is changed
}

void* SortedFile::instantiate_BigQ(void* arg){

	thread_arguments *args;
	args = (thread_arguments *) arg;

	//cout<<"check   "<<(args.s).runLength;
	//cout<<"t check "<<args->in<<"\n";

	args->b = new BigQ(*(args->in),*(args->out),*((args->s).myOrder),(args->s).runLength);

}

int SortedFile::Create (char *f_path, fType f_type, void *startup) {	// done
	file->Open(0,f_path);	

	fileName = (char *)malloc(sizeof(f_path)+1);
	strcpy(fileName,f_path);
	isDirty=0;
	
	// use startup to get runlength and ordermaker
	si = (SortInfo *) startup;
	pageIndex=1;
	recordIndex = 0;
	endOfFile=1;
	return 1;
}

void SortedFile::Load (Schema &f_schema, char *loadpath) {		// requires BigQ instance done

	if(m!=W){
		m = W;
		isDirty=1;
		// create input, output pipe and BigQ instance
		//if(inPipe==NULL)inPipe = new Pipe(100);	// requires size ?
		//if(outPipe==NULL)outPipe = new Pipe(100);
		if(bq==NULL)bq = new BigQ(*inPipe,*outPipe,*(si->myOrder),si->runLength);
	}

	FILE* tableFile = fopen (loadpath,"r");
	Record temp;//need reference see below, make a record

	while(temp.SuckNextRecord(&f_schema,tableFile)!=0)
		inPipe->Insert(&temp);	// pipe blocks and record is consumed or is buffering required ?

	fclose(tableFile);	
}

int SortedFile::Open (char *f_path) {


	isDirty=0;
	char *fName = new char[20];
	sprintf(fName, "%s.meta", f_path);
//	FILE *f = fopen(fName,"r");

	fileName = (char *)malloc(sizeof(f_path)+1);
	strcpy(fileName,f_path);

	//cout<<"reading metadata"<<endl;
	// to decide what to store in meta file
	// and parse and get sort order and run length
	// requires some kind of de serialization
	// initialize it

	ifstream ifs(fName,ios::binary);

	ifs.seekg(sizeof(fileName)-1);//,ifs.beg);
	
	if(si==NULL){
		si = new SortInfo;
		si->myOrder = new OrderMaker();
	}


	ifs.read((char*)si->myOrder, sizeof(*(si->myOrder))); 


	//cout<<"read ordermaker"<<endl;
	//si->myOrder->Print();

/*	if (ifs)
      		cout << "all characters read successfully.";
    	else
    		cout << "error: only " << ifs.gcount() << " could be read";
*/

	//ifs.seekg(sizeof(*(si->myOrder))-1);

	ifs.read((char*)&(si->runLength), sizeof(si->runLength));


//	cout<<"read run length "<<si->runLength<<endl;


	//ofs.write((char*)si->myOrder,sizeof(*(si->myOrder)));	
	//ofs.write((char*)&(si->runLength),sizeof(si->runLength));



	//ifs.sync();

	/*if (ifs)
      		cout << "all characters read successfully.";
    	else
    		cout << "error: only " << ifs.gcount() << " could be read";
*/


	ifs.close();

	m = R;


	//cout<<"size of f name "<<sizeof(fileName)-1<<endl;

	//cout<<"Dereferencing si\n";
	//cout<<"si "<<si<<endl;

	//sleep(2);

	//cout<<"si runlength\n";
	//cout<<"si "<<si->runLength<<endl;

	//cout<<"Dereferencing si ordermaker \n";
	//cout<<"si OM "<<si->myOrder<<endl;

	//MoveFirst();

	// set to read mode
	// bring first page into read buffer and initialize first record

	//fclose(f);

	file->Open(1, f_path);	// open the corresponding file
	pageIndex = 1;
	recordIndex = 0;
	endOfFile = 0;
}

void SortedFile::MoveFirst () {			// requires MergeFromOuputPipe()

	isDirty=0;	
	pageIndex = 1;
	recordIndex = 0;

	if(m==R){
		// In read mode, so direct movefirst is possible

	
		if(file->GetLength()!=0){
			file->GetPage(readPageBuffer,pageIndex); //TODO: check off_t type,  void GetPage (Page *putItHere, off_t whichPage)
	
			int result = readPageBuffer->GetFirst(current);
			//cout<<result<<endl;
			
		//	pageIndex = 1;
			
		}
		else{
		//	pageIndex = 1;

		}

	}
	else{
		// change mode to read
		m = R;
		// Merge contents if any from BigQ
		MergeFromOutpipe();
		file->GetPage(readPageBuffer,pageIndex); //TODO: check off_t type,  void GetPage (Page *putItHere, off_t whichPage)
		readPageBuffer->GetFirst(current);

		
		// bring the first page into readPageBuffer
		// Set curr Record to first record
		// 
	}
	queryChange = true;
}

int SortedFile::Close () {			// requires MergeFromOuputPipe()	done
	
	if(m==W)	
		MergeFromOutpipe();

	file->Close();
	isDirty=0;
	endOfFile = 1;
	// write updated state to meta file

	char fName[30];
	sprintf(fName,"%s.meta",fileName);

	ofstream out(fName);
    	out <<"sorted"<<endl;
    	out.close();


	ofstream ofs(fName,ios::binary|ios::app);

	ofs.write((char*)si->myOrder,sizeof(*(si->myOrder)));	
	ofs.write((char*)&(si->runLength),sizeof(si->runLength));

	ofs.close();
}

void SortedFile::Add (Record &rec) {	// requires BigQ instance		done

	//cout<<"check "<<inPipe<<"\n";
	

	inPipe->Insert(&rec);
	//inPipe->ShutDown();
	//cout<<m<<"\n";

	if(m!=W){
		isDirty=1;
		m = W;
		//cout<<m<<"\n";

		// create input, output pipe and BigQ instance
		/*if(inPipe==NULL){
			inPipe = new Pipe(100);	// requires size ?
			cout<<"Setting up input pipe\n";		
		}
		if(outPipe==NULL){
			outPipe = new Pipe(100);
			cout<<"Setting up output pipe\n";
		}*/

		inPipe= new Pipe(100);
		outPipe= new Pipe(100);

		if(bq==NULL){

			//cout<<"run length "<<si->runLength<<"\n";
			thread_args.in = inPipe;
			thread_args.out = outPipe;
			thread_args.s.myOrder = si->myOrder;
			thread_args.s.runLength =  si->runLength;
			thread_args.b = bq;

			

			pthread_create(&bigQ_t, NULL, &SortedFile::instantiate_BigQ , (void *)&thread_args);


			//cout<<"Setting up BigQ"<<endl;
		}
	}

	queryChange = true;

	//cout<<"record adr"<<&rec<<"\n";
	

	//cout << inPipe<<"\n";
	//inPipe= new Pipe(100);
	
		// pipe blocks and record is consumed or is buffering required ?

	//cout <<"Added\n";
}

int SortedFile::GetNext (Record &fetchme) {		// requires MergeFromOuputPipe()		done

	if(m!=R){
		isDirty=0;
		m = R;
		readPageBuffer->EmptyItOut();		// requires flush
		MergeFromOutpipe();		// 
		MoveFirst();	// always start from first record

	}

	if(endOfFile==1) return 0;

	fetchme.Copy(current);

	if(!readPageBuffer->GetFirst(current)) {

		if(pageIndex>=this->file->GetLength()-2){
				endOfFile = 1;
				return 0;	
		}
		else {
			pageIndex++;
			file->GetPage(readPageBuffer,pageIndex);
			readPageBuffer->GetFirst(current);

		}
	}

	return 1;
}

int SortedFile::GetNext (Record &fetchme, CNF &cnf, Record &literal) {		// requires binary search // requires MergeFromOuputPipe()

	//cout<<"1. here\n";


	//Schema nu("catalog","lineitem");
	//literal.Print(&nu);

	if(m!=R){
	
		isDirty=0;
		m = R;
		readPageBuffer->EmptyItOut();		// requires flush
		MergeFromOutpipe();		// 
		MoveFirst();	// always start from first record
		
		//cout<<"2. here\n";

	
	}	

	//cout<<"3. here\n";
		
	// TODO: update queryChange
	
	if(queryChange != true){		// current query same last CNF query
	
	}
	else{				// query changed ; need to construct new queryOrder
	
		//cout<<"4. here\n"<<si<<" addr \n";
		queryOrder = cnf.CreateQueryMaker(*(si->myOrder));		
		//queryOrder = checkIfMatches(cnf, *(si->myOrder));
		
		
	}
	
	ComparisonEngine *comp;
		
	if(queryOrder==NULL) {		// no compatible order maker; return first record that matches the Record literal
			
		//cout<<"Empty Query OM\n";		

		while(GetNext(fetchme)){			// linear scan from current record

			
			if(comp->Compare(&fetchme, &literal, &cnf)) {		//record found, return 1
				return 1;
			}
		}
		return 0;					// record not found
	}
	else{							 // some order maker compatible to given CNF is constructed
	
		Record *r1 = new Record();

		cout<<"matchpage called using "<< queryOrder<<endl;
			
		r1 = GetMatchPage(literal);
		
		if(r1==NULL) return 0;
		
		fetchme.Consume(r1);
		
		if(comp->Compare(&fetchme,&literal,&cnf)){
			 return 1;
		}
		
		while(GetNext(fetchme)) {
			if(comp->Compare(&fetchme, &literal, queryOrder)!=0) {
				//not match to query order
				return 0;
			} else {
				if(comp->Compare(&fetchme, &literal, &cnf)) {
					//find the right record
					return 1;
				}
			}
		}
	
	}
	return 0;
}


OrderMaker *SortedFile::checkIfMatches(CNF &cnf, OrderMaker &sortOrder) {
	

	OrderMaker *matchOrder= new OrderMaker;	// create new order make
	//cout<<"ordermaker";
	//sortOrder.Print();
	//cout<<"\n";
	//cout<<"cnf";
	//cnf.Print();

	
	for(int i = 0; i<sortOrder.numAtts; i++) {	// over every attribute of sortorder

		//cout <<"now checking att: "<<sortOrder.whichAtts[i]<<endl;
		//cout<<sortOrder.numAtts<<"\n";
		//cout<<sortOrder.whichTypes[i]<<"\n";

		bool match = false;
		
		for(int j = 0; j<cnf.numAnds; j++) {			// 

			if(!match) {
				
				for(int k=0; k<cnf.orLens[j]; k++) {	//
					
					//cout <<"now try matching cnf att: "<<cnf.orList[j][k].whichAtt1<< " Op2: "<<cnf.orList[j][k].whichAtt2<<endl;

					if(cnf.orList[j][k].op == Equals) {
		
						if(cnf.orList[j][k].operand1 == Literal) {
	
							//cout<<sortOrder.whichAtts[i]<<"\t"<<cnf.orList[j][k].whichAtt1;
							//cout<<"\n"<<sortOrder.whichTypes[i]<<"\t"<<cnf.orList[j][k].attType;
	
							if((sortOrder.whichAtts[i] == cnf.orList[j][k].whichAtt1)
									&& (sortOrder.whichTypes[i] == cnf.orList[j][k].attType)){
								
								matchOrder->whichAtts[matchOrder->numAtts] = sortOrder.whichAtts[i];
								matchOrder->whichTypes[matchOrder->numAtts++] = sortOrder.whichTypes[i];
								match = true;
								
								//cout<<"matchs!!";

								//matchOrder->Print();
								break;
							}
	
						} else if(cnf.orList[j][k].operand2 == Literal) {
						
							if((sortOrder.whichAtts[i] == cnf.orList[j][k].whichAtt2)
									&& (sortOrder.whichTypes[i] == cnf.orList[j][k].attType)){
						
								matchOrder->whichAtts[matchOrder->numAtts] = sortOrder.whichAtts[i];
								matchOrder->whichTypes[matchOrder->numAtts++] = sortOrder.whichTypes[i];
								match = true;
								//cout<<"match1";
								//matchOrder->Print();
								break;
							}
						}
					}
				}
			}
		}
		if(!match) break;
	}
	if(matchOrder->numAtts == 0)
	{
		cout <<"No query OrderMaker can be constructed!"<<endl;
		delete matchOrder;
		return NULL;
	}

	//cout<<"BS OM constructed";
	return matchOrder;
}



Record* SortedFile::GetMatchPage(Record &literal) {			//returns the first record which equals to literal based on queryorder;
	
	//cout<<"pi3"<<pageIndex;
	if(queryChange) {
		int low = pageIndex;
		int high = file->GetLength()-2;
		int matchPage = bsearch(low, high, queryOrder, literal);
		//cout <<"matchpage: "<<matchPage<<endl;
		if(matchPage == -1) {
			//not found
			return NULL;
		}
		if(matchPage != pageIndex) {
			readPageBuffer->EmptyItOut();
			file->GetPage(readPageBuffer, matchPage);
			pageIndex = matchPage+1;
		}
		queryChange = false;
	}

	//find the potential page, make reader buffer pointer to the first record
	// that equal to query order
	Record *returnRcd = new Record;
	ComparisonEngine cmp1;
	while(readPageBuffer->GetFirst(returnRcd)) {
		if(cmp1.Compare(returnRcd, &literal, queryOrder) == 0) {
			//find the first one
			return returnRcd;
		}
	}
	if(pageIndex >= file->GetLength()-2) {
		return NULL;
	} else {
		//since the first record may exist on the next page
		pageIndex++;
		file->GetPage(readPageBuffer, pageIndex);
		while(readPageBuffer->GetFirst(returnRcd)) {
			if(cmp1.Compare(returnRcd, &literal, queryOrder) == 0) {
				//find the first one
				return returnRcd;
			}
		}
	}
	return NULL;
		

}

int SortedFile::bsearch(int low, int high, OrderMaker *queryOM, Record &literal) {
	
	cout<<"serach OM "<<endl;
	queryOM->Print();
	cout<<endl<<"file om"<<endl;
	si->myOrder->Print();

	if(high < low) return -1;
	if(high == low) return low;
	//high > low
	
	ComparisonEngine *comp;
	Page *tmpPage = new Page;
	Record *tmpRcd = new Record;
	int mid = (int) (high+low)/2;
	file->GetPage(tmpPage, mid);
	
	int res;

	//cout<<"l "<<low<<" m "<<mid<<" h "<<high<<endl;
	Schema nu("catalog","lineitem");

	//tmpPage->GetFirst(tmpRcd) == 1;

	tmpPage->GetFirst(tmpRcd);
	
	///cout<<"TeMP"<<endl<<endl<<endl;
	//tmpRcd->Print(&nu);
	//cout<<"literal"<<endl<<endl<<endl;
	//cout<<&literal;//.Print(&nu);

	tmpRcd->Print(&nu);

		res = comp->Compare(tmpRcd,si->myOrder, &literal,queryOM );

		//if(res==0){
			//cout<<"FOUND!!!"<<endl;
	//		break;
//		}
	

	delete tmpPage;
	delete tmpRcd;

	//cout<<"compare result"<<res<<"\n";


	if( res == -1) {
		if(low==mid)
			return mid;
		return bsearch(low, mid-1, queryOM, literal);
	}
	else if(res == 0) {
		return mid;//bsearch(low, mid-1, queryOM, literal);
	}
	else
		return bsearch(mid+1, high,queryOM, literal);
}




/*
OrderMaker* SortedFile::checkIfMatches(CNF &c, OrderMaker &o) {

	OrderMaker *query = new OrderMaker();	// ordermaker that we try to build
	bool matches = false;

	cout<<"o addr " <<&o;
	
	for(int i=0;i<o.numAtts;i++)	// Over every attribute of file's ordermaker
	{
		matches = false;
		
		for(int j = 0; j<c.numAnds; j++) {	// Over list of all disjunctions

			if(!matches) {
			
				for(int k=0; k<c.orLens[j]; k++) {	// over every comparison
				
					if(c.orList[j][k].op == Equals){	// check is operator is 'Equals'
						
						if(c.orList[j][k].operand1 == Literal) {		// check if operand is 'Literal'
							
							if((o.whichAtts[i] == c.orList[j][k].whichAtt1)		// 		attribute and type matches 
										&& (o.whichTypes[i] == c.orList[j][k].attType)){
										
									query->whichAtts[query->numAtts] = o.whichAtts[i];
									query->whichTypes[query->numAtts++] = o.whichTypes[i];
									matches = true;
									break;
							}
						} else if(c.orList[j][k].operand2 == Literal) {
							
							if((o.whichAtts[i] == c.orList[j][k].whichAtt2)
									&& (o.whichTypes[i] == c.orList[j][k].attType)){
									
								query->whichAtts[query->numAtts] = o.whichAtts[i];
								query->whichTypes[query->numAtts++] = o.whichTypes[i];
								matches = true;
								break;
							}
						}
					}				
				}
			}
		if(!matches) break; 	// this happens 
		}
	}

	if(query->numAtts == 0)
	{
		delete query;
		return NULL;
	}
	return query;
	
	
}
*/
/*
Record* SortedFile::GetMatchPage(Record &literal) {			//returns the first record which equals to literal based on queryorder;
		
	int low = pageIndex;
	int high = file->GetLength()-2;
	
	int matchPage = bsearch(low, high, queryOrder, literal);
	if(matchPage == -1) {	//not found

		cout<<"No page found"<<endl;
		return NULL;		

	}
	if(matchPage != pageIndex-1) {
		readPageBuffer->EmptyItOut();
		file->GetPage(readPageBuffer, matchPage);
		pageIndex = matchPage+1;
	}
	queryChange = false;

	//find the potential page, make reader buffer pointer to the first record
	// that equal to query order
	ComparisonEngine *comp;
	Record *returnRcd = new Record;
	while(readPageBuffer->GetFirst(returnRcd)) {
		if(comp->Compare(returnRcd, &literal, queryOrder) == 0) {
			//find the first one
			return returnRcd;
		}
	}
	if(pageIndex >= file->GetLength()-2) {

		cout<<"Reached EOF"<<endl;
		return NULL;

	} else {
		//since the first record may exist on the next page
		pageIndex++;
		file->GetPage(readPageBuffer, pageIndex);
		while(readPageBuffer->GetFirst(returnRcd)) {
			if(comp->Compare(returnRcd, &literal, queryOrder) == 0) {
				//find the first one
				return returnRcd;
			}
		}
	}
	return NULL;

}

int SortedFile::bsearch(int low, int high, OrderMaker *queryOM, Record &literal) {
	
	ComparisonEngine *comp;
	if(high < low) return -1;
	if(high == low) return low;
	//high > low
	Page *tmpPage = new Page;
	Record *tmpRcd = new Record;
	int mid = (int) (high+low)/2;
	file->GetPage(tmpPage, mid);
	tmpPage->GetFirst(tmpRcd);
	int res = comp->Compare(tmpRcd, &literal, queryOM);
	if( res == -1) {
		if(low==mid)
			return mid;
		return bsearch(mid, high, queryOM, literal);
	}
	else if(0 == res) {
		return bsearch(low, mid-1, queryOM, literal);
	}
	else
		return bsearch(low, mid-1, queryOM, literal);
}
*/

void SortedFile:: MergeFromOutpipe(){		// requires both read and write modes

	// close input pipe
	//cout<<inPipe<<endl;

	inPipe->ShutDown();
	// get sorted records from output pipe
	ComparisonEngine *ce;

	// following four lines get the first record from those already present (not done)
	if(!tobeMerged){ tobeMerged = new Page(); }
	pagePtrForMerge = 0; 
	Record *rFromFile = new Record();
	GetNew(rFromFile);						// loads the first record from existing records

	Record *rtemp = new Record();		
	Page *ptowrite = new Page();			// new page that would be added
	File *newFile = new File();				// new file after merging
	newFile->Open(0,"mergedFile");				

	bool nomore = false;
        int result =GetNew(rFromFile);
	int pageIndex = 1;


	Schema nu("catalog","lineitem");


	if(result==0)
		nomore =true;

	//cout<<"nomore is "<<nomore<<endl;

	while(isDirty!=0&&!nomore){

		//cout<<" rtemp "<<rtemp<<" rfile "<<rFromFile<<"\n";
		

		if(outPipe->Remove(rtemp)==1){		// got the record from out pipe

			//rtemp->Print(&nu);

			while(ce->Compare(rFromFile,rtemp,si->myOrder)<0){ 		// merging this record with others

				if(ptowrite->Append(rFromFile)==0){		// merge already existing record
						// page full
						
						//int pageIndex = newFile->GetLength()==0? 0:newFile->GetLength()-1;

						//*
						// write this page to file


						//cout<<"1. write at index "<<pageIndex<<endl;



						newFile->AddPage(ptowrite,pageIndex++);
						//pageIndex++;
						// empty this out
						ptowrite->EmptyItOut();
						// append the current record ?
						ptowrite->Append(rFromFile);		// does this consume the record ?
						
				}

				//cout<<"1. rfile "<<rFromFile<<"\n";

				if(!GetNew(rFromFile)){ nomore = true; break; }	// bring next rFromFile record ?// check if records already present are exhausted

			}


			if(ptowrite->Append(rtemp)!=1){				// copy record from pipe
						// page full
						
						//int pageIndex = newFile->GetLength()==0? 0:newFile->GetLength()-1;


						//*
						// write this page to file


						//cout<<"2. write at index "<<pageIndex<<endl;


						newFile->AddPage(ptowrite,pageIndex++);
						// empty this out
						ptowrite->EmptyItOut();
						// append the current record ?
						ptowrite->Append(rtemp);		// does this consume the record ?
			}

		}
		else{
			// pipe is empty now, copy rest of records to new file
			do{

				//cout<<"2. rfile "<<rFromFile<<"\n";	
				
				if(ptowrite->Append(rFromFile)!=1){			

					//int pageIndex = newFile->GetLength()==0? 0:newFile->GetLength()-1;	// page full
					//*
					// write this page to file


					//cout<<"3. write at index "<<pageIndex<<endl;


					newFile->AddPage(ptowrite,pageIndex++);
					// empty this out
					ptowrite->EmptyItOut();
					// append the current record ?
					ptowrite->Append(rFromFile);		// does this consume the record ?
					
				}
			}while(GetNew(rFromFile)!=0);
			break;
		}
	}

	outPipe->Remove(rtemp);//1 missing record

	

	if(nomore==true){									// file is empty
		do{

			//rtemp->Print(&nu);	
			//cout<<"inbetween\n";
		
			if(ptowrite->Append(rtemp)!=1){				// copy record from pipe
						//int pageIndex = newFile->GetLength()==0? 0:newFile->GetLength()-1;		// page full
						// write this page to file
						//cout<<"write at index "<<pageIndex<<endl;
						newFile->AddPage(ptowrite,pageIndex++);
						// empty this out
						ptowrite->EmptyItOut();
						// append the current record ?
						ptowrite->Append(rtemp);		// does this consume the record ?
						
	
			}
		}while(outPipe->Remove(rtemp)!=0);
	}

	newFile->AddPage(ptowrite,pageIndex);//newFile->GetLength()-1);
	//cout<<"last write at index "<<pageIndex<<endl;


	newFile->Close();
	file->Close();

	// delete resources that are not required

	if(rename(fileName,"mergefile.tmp")!=0) {				// making merged file the new file
		cerr <<"rename file error!"<<endl;
		return;
	}
	
	remove("mergefile.tmp");

	if(rename("mergedFile",fileName)!=0) {				// making merged file the new file
		cerr <<"rename file error!"<<endl;
		return;
	}

	readPageBuffer->EmptyItOut();




	file->Open(1, this->fileName);

}


int SortedFile:: GetNew(Record *r1){

	while(!this->tobeMerged->GetFirst(r1)) {
		if(pagePtrForMerge >= file->GetLength()-1)
			return 0;
		else {
			file->GetPage(tobeMerged, pagePtrForMerge);
			pagePtrForMerge++;
		}
	}

	return 1;
}	


SortedFile::~SortedFile() {
	delete readPageBuffer;
	delete inPipe;
	delete outPipe;
}
