#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "bb.h"
#include "class.h"

#define DBG		0

typedef struct LinkedListNode{

	struct LinkedListNode* next;
	struct LinkedListNode* prev;

}LLN;

typedef struct{

	LLN list;

	uint32_t start;

}QueueNode;

typedef struct BB{

	LLN list;

	//initial values
	uint32_t initialAddr;
	uint32_t initialLen;

	//final values
	uint32_t addr;
	uint32_t len;

	uint32_t numInstr;
	Instr* instrs;		//separate array to aid in resizing

}BB;

typedef struct ExcEntry{

	LLN list;

	uint32_t initialStart;
	uint32_t initialEnd;
	uint32_t initialHandler;

	BB* start;
	BB* end;
	BB* handler;

}ExcEntry;




LLN bbList;
LLN excList;
LLN Q;

static const uint8_t* gCode;
static uint32_t gCodeLen;


static void bbPrvInsertAddr(uint32_t addr);


void* alloc(uint32_t sz){

	void* ret;

	if(sz == 0) sz++;


	ret = malloc(sz);
	if(!ret){

		fprintf(stderr, "Alloc of %" PRIu32 " bytes fail!\n", sz);
		exit(-10);
	}

	return ret;
}

void mfree(void* ptr){

	if(ptr){

		free(ptr);
	}
}

static void bbVerifyNewType(const char *str, uint16_t len)
{
	if(!strncmp("java/lang/String", str, len)) return;
	if(!strncmp("java/lang/StringBuilder", str, len)) return;

	fprintf(stderr, "ERROR: Attempting to create instance of illegal class %.*s\n", len, str);
	exit(-77);
}

static BB* bbPrvDisasm(JavaClass *c, const uint8_t* code, uint32_t pos, uint32_t len){

	uint32_t pc = pos;
	uint32_t t1, t2, t3;
	BB* bb = alloc(sizeof(BB));
	char wide = 0;

	bb->initialAddr = pos;
	bb->initialLen = len;

	bb->numInstr = 0;
	bb->instrs = alloc(0);

	while(pc < pos + len){

		uint32_t initialPC = pc;
		uint32_t extraSz = 0;
		signed short ofst16;
		signed long ofst32;
		Instr* i = realloc(bb->instrs, sizeof(Instr[bb->numInstr + 1]));
		uint32_t j, wideHandled = 1;

		if(!i){

			fprintf(stderr, "instrs realloc failed\n");
			exit(-3);
		}

		bb->instrs = i;
		i += bb->numInstr++;

		bzero(i, sizeof(Instr));	//required [{ ! }]

	redo:
		if(wide){
			if(i->wide){

				fprintf(stderr, "wide wide\n");
				exit (-11);
			}
			i->wide = 1;
			wideHandled = 0;
		}
		wide = 0;

		switch(i->type = code[pc++]){

			case 0x10:			//bipush
			case 0x12:			//ldc
			case 0xBC:			//newarray

				extraSz = 1;
				break;

			case 0x11:			//sipush
			case 0x13:			//ldc_w
			case 0x14:			//ldc2_w
			case 0xB6:			//invokevirtual
			case 0xBB:			//new
			case 0xBD:			//anewarray
			case 0xC0:			//checkcast
			case 0xC1:			//instanceof

				extraSz = 2;
				break;

			case 0x00 ... 0x0F:
			case 0x1A ... 0x35:
			case 0x3B ... 0x83:
			case 0x85 ... 0x98:
			case 0xAC ... 0xB1:
			case 0xBE ... 0xBF:
			case 0xC2 ... 0xC3:

				//single byte instrs
				break;

			case 0x15:			//iload
			case 0x16:			//lload
			case 0x17:			//fload
			case 0x18:			//dload
			case 0x19:			//aload
			case 0x36:			//istore
			case 0x37:			//lstore
			case 0x38:			//fstore
			case 0x39:			//dstore
			case 0x3A:			//astore
			case 0xA9:			//ret

				extraSz = i->wide ? 2 : 1;
				wideHandled = 1;
				break;

			case 0x84:			//iinc

				extraSz = i->wide ? 4 : 2;
				wideHandled = 1;
				break;

			case INSTR_TYPE_TABLESWITCH:	//tableswitch

				extraSz = 3 - (initialPC & 3);

				t3 = code[pc++ + extraSz];	//calculate default branch dest
				t3 <<= 8;
				t3 += code[pc++ + extraSz];
				t3 <<= 8;
				t3 += code[pc++ + extraSz];
				t3 <<= 8;
				t3 += code[pc++ + extraSz];

				t3 += initialPC;

				t1 = code[pc++ + extraSz];	//calculate lo
				t1 <<= 8;
				t1 += code[pc++ + extraSz];
				t1 <<= 8;
				t1 += code[pc++ + extraSz];
				t1 <<= 8;
				t1 += code[pc++ + extraSz];

				t2 = code[pc++ + extraSz];	//calculate hi
				t2 <<= 8;
				t2 += code[pc++ + extraSz];
				t2 <<= 8;
				t2 += code[pc++ + extraSz];
				t2 <<= 8;
				t2 += code[pc++ + extraSz];

				i->switches.numCases = t2 - t1 + 1;	//calculate number of entries
				i->switches.first = t1;

				//alloc tables
				i->switches.initialDestAddrs = alloc(sizeof(uint32_t[i->switches.numCases + 1]));
				i->switches.destOffsets = alloc(sizeof(int32_t[i->switches.numCases + 1]));
				i->switches.dests = alloc(sizeof(BB*[i->switches.numCases + 1]));

				//fill tables as needed
				i->switches.initialDestAddrs[i->switches.numCases] = t3;
				bbPrvInsertAddr(t3);

				for(t1 = 0; t1 < i->switches.numCases; t1++){

					t2 = code[pc++ + extraSz];	//calculate offset
					t2 <<= 8;
					t2 += code[pc++ + extraSz];
					t2 <<= 8;
					t2 += code[pc++ + extraSz];
					t2 <<= 8;
					t2 += code[pc++ + extraSz];

					t2 += initialPC;

					i->switches.initialDestAddrs[t1] = t2;
					bbPrvInsertAddr(t2);
				}
				break;

			case INSTR_TYPE_LOOKUPSWITCH:	//lookupswitch

				extraSz = 3 - (initialPC & 3);

				t3 = code[pc++ + extraSz];	//calculate default branch dest
				t3 <<= 8;
				t3 += code[pc++ + extraSz];
				t3 <<= 8;
				t3 += code[pc++ + extraSz];
				t3 <<= 8;
				t3 += code[pc++ + extraSz];

				t3 += initialPC;

				t1 = code[pc++ + extraSz];	//calculate numCases
				t1 <<= 8;
				t1 += code[pc++ + extraSz];
				t1 <<= 8;
				t1 += code[pc++ + extraSz];
				t1 <<= 8;
				t1 += code[pc++ + extraSz];

				i->switches.numCases = t1;

				//alloc tables
				i->switches.matchVals = alloc(sizeof(uint32_t[i->switches.numCases]));
				i->switches.initialDestAddrs = alloc(sizeof(uint32_t[i->switches.numCases + 1]));
				i->switches.destOffsets = alloc(sizeof(int32_t[i->switches.numCases + 1]));
				i->switches.dests = alloc(sizeof(BB*[i->switches.numCases + 1]));

				//fill tables as needed
				i->switches.initialDestAddrs[i->switches.numCases] = t3;
				bbPrvInsertAddr(t3);

				for(t1 = 0; t1 < i->switches.numCases; t1++){

					t2 = code[pc++ + extraSz];	//calculate match val
					t2 <<= 8;
					t2 += code[pc++ + extraSz];
					t2 <<= 8;
					t2 += code[pc++ + extraSz];
					t2 <<= 8;
					t2 += code[pc++ + extraSz];

					i->switches.matchVals[t1] = t2;

					t2 = code[pc++ + extraSz];	//calculate offset
					t2 <<= 8;
					t2 += code[pc++ + extraSz];
					t2 <<= 8;
					t2 += code[pc++ + extraSz];
					t2 <<= 8;
					t2 += code[pc++ + extraSz];

					t2 += initialPC;

					i->switches.initialDestAddrs[t1] = t2;
					bbPrvInsertAddr(t2);
				}
				break;

			case INSTR_TYPE_WIDE:		//wide

				wide = 1;
				goto redo;

			case INSTR_TYPE_PUSH_RAW:	//my instr

				extraSz = 4;
				break;

			case 0xB7:			//invokespecial
			case 0xB8:			//invokestatic
			case 0xB2:			//getstatic
			case 0xB3:			//putstatic
			case 0xB4:			//getfield
			case 0xB5:			//putfield

				extraSz = i->wide ? 3 : 2;
				wideHandled = 1;
				break;

			case 0xB9:			//invokeinterface
			case 0xBA:			//invokedynamic

				extraSz = 4;
				break;

			case 0xC5:			//multinewarray

				extraSz = 3;
				break;

			case 0x99:			//ifeq
			case 0x9A:			//ifne
			case 0x9B:			//iflt
			case 0x9C:			//ifge
			case 0x9D:			//ifgt
			case 0x9E:			//ifle
			case 0x9F:			//if_icmpeq
			case 0xA0:			//if_icmpne
			case 0xA1:			//if_icmplt
			case 0xA2:			//if_icmpge
			case 0xA3:			//if_icmpgt
			case 0xA4:			//if_icmple
			case 0xA5:			//if_acmpeq
			case 0xA6:			//if_acmpne
			case 0xA7:			//goto
			case 0xA8:			//jsr
			case 0xC6:			//ifnull
			case 0xC7:			//ifnonnull

				extraSz = 0;
				ofst16 = code[pc + extraSz++];
				ofst16 <<= 8;
				ofst16 += code[pc + extraSz++];
				i->destLen = 2;
				i->destOrigAddr = initialPC + (signed long)ofst16;
				break;

			case 0xC8:			//goto_w
			case 0xC9:			//jsr_w

				extraSz = 0;
				ofst32 = code[pc + extraSz++];
				ofst32 <<= 8;
				ofst32 += code[pc + extraSz++];
				ofst32 <<= 8;
				ofst32 += code[pc + extraSz++];
				ofst32 <<= 8;
				ofst32 += code[pc + extraSz++];
				i->destLen = 4;
				i->destOrigAddr = initialPC + ofst32;
				break;

			default:

				fprintf(stderr, "unhandled instr 0x%02X\n", i->type);
				exit(-6);
		}

		if(!wideHandled){

			fprintf(stderr, "unhandled wide 0x%02X\n", i->type);
			exit(-6);
		}

		if(i->destLen) bbPrvInsertAddr(i->destOrigAddr);

		if(DBG) fprintf(stderr, "INSTR(0x%06" PRIX32 "): %s%02X", initialPC, i->wide ? "C4 ":"", i->type);

		if(extraSz > sizeof(i->bytes)){

			fprintf(stderr, "Too much data for an instruction: %" PRIu32 " bytes\n", extraSz);
			exit(-22);
		}
		i->numBytes = extraSz;
		for(j = 0; j < extraSz; j++){

			i->bytes[j] = code[pc++];
			if(DBG) fprintf(stderr, " %02X", i->bytes[j]);
		}

		if (i->type == 0xBB){
			uint16_t offst = ((i->bytes[0] << 8) | i->bytes[1]) - 1;
			JavaConstant *jc = c->constantPool[offst];
			if (jc->type == JAVA_CONST_TYPE_CLASS){
				offst = ((uint16_t*)(c->constantPool[offst] + 1))[0] - 1;
				jc = c->constantPool[offst];
				if (jc->type == JAVA_CONST_TYPE_STRING) {
					JavaString *jstr = (JavaString*)(jc + 1);
					bbVerifyNewType(jstr->data, jstr->len);
				} else {
					fprintf(stderr, "unknown inner type %d\n", (int)jc->type);
				}
			} else {
				fprintf(stderr, "unknown type %d\n", (int)jc->type);
			}
		}

		if(DBG) fprintf(stderr, "\n");
	}

	return bb;
}

static void llRemove(LLN* lli){

	lli->prev->next = lli->next;
	lli->next->prev = lli->prev;

	lli->prev = 0;
	lli->next = 0;
}

static void llInsertAfter(LLN* insertAfterThis, LLN* insertThis){

	insertThis->next = insertAfterThis->next;
	insertThis->next->prev = insertThis;
	insertThis->prev = insertAfterThis;
	insertAfterThis->next = insertThis;
}

static void llInit(LLN* ll){

	ll->next = ll;
	ll->prev = ll;
}

static void bbPrvInstrFree(Instr* i){

	mfree(i->switches.matchVals);
	mfree(i->switches.destOffsets);
	mfree(i->switches.dests);
	mfree(i->switches.initialDestAddrs);
}

static void bbPrvInstrExportWrite32(uint8_t** bufP, int32_t val){

	if(!*bufP) return;

	*(*bufP)++ = val >> 24;
	*(*bufP)++ = val >> 16;
	*(*bufP)++ = val >> 8;
	*(*bufP)++ = val;
}

static uint32_t bbPrvInstrExport(const Instr* i, uint8_t* buf, uint32_t pc){	//return len

	uint8_t padBytes;
	uint32_t j;
	uint32_t len = 1;	//instr itself


	if(DBG) fprintf(stderr, "exporting %s %02X (nb=%" PRIu32 ")->", i->wide ? "wide" : "", i->type, i->numBytes);

	if(i->wide){
		if(buf) *buf++ = INSTR_TYPE_WIDE;
		len++;
	}
	if(buf) *buf++ = i->type;

	if(i->type == INSTR_TYPE_TABLESWITCH){

		//padBytes calculation (for padding)
		padBytes = 3 - (pc & 3);
		for(j = 0; j < padBytes; j++){

			if(buf) *buf++ = 0x00;
			len++;
		}

		//default case
		bbPrvInstrExportWrite32(&buf, i->switches.destOffsets[i->switches.numCases]);
		len += 4;

		//lo
		bbPrvInstrExportWrite32(&buf, i->switches.first);
		len += 4;

		//hi
		bbPrvInstrExportWrite32(&buf, i->switches.first + i->switches.numCases - 1);
		len += 4;

		//cases
		for(j = 0; j < i->switches.numCases; j++){

			bbPrvInstrExportWrite32(&buf, i->switches.destOffsets[j]);
			len += 4;
		}
	}
	else if(i->type == INSTR_TYPE_LOOKUPSWITCH){

		//padBytes calculation (for padding)
		padBytes = 3 - (pc & 3);
		for(j = 0; j < padBytes; j++){

			if(buf) *buf++ = 0x00;
			len++;
		}

		//default case
		bbPrvInstrExportWrite32(&buf, i->switches.destOffsets[i->switches.numCases]);
		len += 4;

		//numCases
		bbPrvInstrExportWrite32(&buf, i->switches.numCases);
		len += 4;

		//cases
		for(j = 0; j < i->switches.numCases; j++){

			bbPrvInstrExportWrite32(&buf, i->switches.matchVals[j]);
			bbPrvInstrExportWrite32(&buf, i->switches.destOffsets[j]);
			len += 8;
		}
	}
	else if(i->destLen){ 	//simple branch

		switch(i->destLen){

			case 4: if(buf) *buf++ = i->finalOffset >> 24;
				if(buf) *buf++ = i->finalOffset >> 16;
				len += 2;
				/* fall-thru */
			case 2:	if(buf) *buf++ = i->finalOffset >> 8;
				if(buf) *buf++ = i->finalOffset;
				len += 2;
				break;
			default:

				fprintf(stderr, "Weird destination size in branch: %" PRIu32 "\n", i->destLen);
				exit(-19);
		}
	}
	else{			//somethign else - just emit the bytes

		for(j = 0; j < i->numBytes; j++){

			if(buf) *buf++ = i->bytes[j];
			len++;
		}
	}

	if(DBG) fprintf(stderr, "%" PRIu32 "\n", len);

	return len;
}

#define FIND_CONTANING			0
#define FIND_CONTAINING_OR_NEXT		1
#define FIND_EXACT_ADDR			2

static BB* bbPrvFind(uint32_t addr, uint8_t findType){	//find BB containing given adr (or next one after it)

	LLN* n = &bbList;

	while((n = n->next) != &bbList){

		BB* bb = (BB*)n;

		switch(findType){

			case FIND_CONTANING:

				if(bb->initialAddr <= addr && bb->initialAddr + bb->initialLen > addr) return bb;
				break;

			case FIND_CONTAINING_OR_NEXT:

				if(bb->initialAddr > addr) return bb;
				break;

			case FIND_EXACT_ADDR:

				if(bb->initialAddr == addr) return bb;
				break;
		}
	}

	return 0;
}

static void bbTruncate(BB* bb, uint32_t newLen){	//not not call this anytime if addr != initialAddr (after code size changes) as it will mess up tableswitch/lookupswitch

	uint32_t len = 0;

	if(DBG) fprintf(stderr, "Truncating 0x%" PRIX32 "+0x%" PRIX32 " to len 0x%" PRIX32 "\n", bb->initialAddr, bb->initialLen, newLen);

	if(newLen > bb->initialLen){

		fprintf(stderr, "Cannot truncate to a longer length!\n");
		exit(-14);
	}
	else if(newLen == bb->initialLen){

		fprintf(stderr, "Should not truncate to same length!\n");
		exit(-15);
	}
	else{
		Instr* i = bb->instrs;
		uint32_t instrsLeft = bb->numInstr;

		while(len < newLen && instrsLeft){

			len += bbPrvInstrExport(i, 0, bb->initialAddr + len);
			i++;
			instrsLeft--;
		}

		if(len != newLen){

			fprintf(stderr, "Truncate failed: len=%" PRIu32 " instrsLeft=%" PRIu32 ", wantedLen=%" PRIu32 "\n", len, instrsLeft, newLen);
			exit(-17);
		}

		bb->numInstr -= instrsLeft;
		bb->initialLen = newLen;

		while(instrsLeft--){

			 bbPrvInstrFree(i++);
		}


		bb->instrs = realloc(bb->instrs, sizeof(Instr[bb->numInstr]));
	}
}

static void bbPrvRun(JavaClass *c){

	LLN* lli;
	QueueNode* n;
	BB* bb;
	BB* bbNew;
	uint32_t len;

	while((lli = Q.next) != &Q){

		n = (QueueNode*)lli;
		llRemove(lli);
		lli = 0;

		bb = bbPrvFind(n->start, FIND_CONTANING);
		if(!bb){

			bb = bbPrvFind(n->start, FIND_CONTAINING_OR_NEXT);
			if(bb){		//we have a place to insert before and know the len

				len = bb->initialAddr - n->start;
				lli = bb->list.prev;	//so we can insert after instead of before ("after prev" = "before cur")
			}
			else{		//nothing after us - sucks

				len = gCodeLen - n->start;
				lli = &bbList;
			}
		}
		else if(bb->initialAddr == n->start){

			//all good - nothing to do
		}
		else{		//we need to re-do this one in pieces

			len = bb->initialLen + bb->initialAddr - n->start;
			bbTruncate(bb, n->start - bb->initialAddr);
			lli = &bb->list;
		}

		if(lli){
			bbNew = bbPrvDisasm(c, gCode, n->start, len);
			llInsertAfter(lli, &bbNew->list);
		}

		mfree(n);
	}
}

static void bbPrvInsertAddr(uint32_t addr){

	QueueNode* n;

	n = alloc(sizeof(QueueNode));

	n->start = addr;
	llInsertAfter(Q.prev, &n->list);	//inserts at end
}

void bbInit(JavaClass *c, uint8_t* code, uint32_t len){

	llInit(&bbList);
	llInit(&excList);
	llInit(&Q);

	gCode = code;
	gCodeLen = len;

	bbPrvInsertAddr(0);
	bbPrvRun(c);

	if(DBG){
		LLN* lli = &bbList;

		while((lli = lli->next) != &bbList){

			fprintf(stderr, "BLOCK: 0x%08" PRIX32 " + 0x%08" PRIX32 "\n", ((BB*)lli)->initialAddr, ((BB*)lli)->initialLen);
		}
	}
}

void bbAddExc(JavaClass *c, uint16_t startpc, uint16_t endpc, uint16_t handler){

	ExcEntry* ee = alloc(sizeof(ExcEntry));

	bbPrvInsertAddr(startpc);
	bbPrvInsertAddr(endpc);
	bbPrvInsertAddr(handler);

	ee->initialStart = startpc;
	ee->initialEnd = endpc;
	ee->initialHandler = handler;

	llInsertAfter(excList.prev, &ee->list);	//insert at end

	bbPrvRun(c);
}

void bbFinishLoading(){

	uint32_t wantedAddr;
	BB* dst;
	LLN* n;

	//resolve instructions' jumps
	n = bbList.next;
	while(n != &bbList){

		BB* bb = (BB*)n;
		uint32_t i, j;

		n = n->next;

		for(i = 0; i < bb->numInstr; i++){

			if((bb->instrs[i].type == INSTR_TYPE_TABLESWITCH) || (bb->instrs[i].type == INSTR_TYPE_LOOKUPSWITCH)){

				for(j = 0; j <= bb->instrs[i].switches.numCases; j++){

					dst = bbPrvFind(wantedAddr = bb->instrs[i].switches.initialDestAddrs[j], FIND_EXACT_ADDR);
					if(!dst) goto fail;

					bb->instrs[i].switches.dests[j] = dst;
				}
			}
			else if(bb->instrs[i].destLen){ 	//simple branch

				dst = bbPrvFind(wantedAddr = bb->instrs[i].destOrigAddr, FIND_EXACT_ADDR);
				if(!dst) goto fail;

				bb->instrs[i].jmpDest = dst;
			}
		}
	}

	//resolve exception jumps
	n = excList.next;
	while(n != &excList){

		ExcEntry* ee = (ExcEntry*)n;

		n = n->next;

		ee->start = bbPrvFind(wantedAddr = ee->initialStart, FIND_EXACT_ADDR);
		if(!ee->start) goto fail;

		ee->end = bbPrvFind(wantedAddr = ee->initialEnd, FIND_EXACT_ADDR);
		if(!ee->end) goto fail;

		ee->handler = bbPrvFind(wantedAddr = ee->initialHandler, FIND_EXACT_ADDR);
		if(!ee->handler) goto fail;
	}

	gCodeLen = 0;
	gCode = 0;

	return;
fail:
	fprintf(stderr, "Branch resolution failed for address 0x%06" PRIX32 "\n", wantedAddr);
	exit(-17);
}

//do a pass over all blocks of this method
void bbPass(BbPassF pf, void* userData){

	LLN* n;

	n = bbList.next;
	while(n != &bbList){

		BB* bb = (BB*)n;
		n = n->next;

		pf(bb->instrs, bb->numInstr, userData);
	}
}

static uint32_t bbPrvExportOneBlock(BB* bb, uint8_t* buf, uint32_t pc){

	uint32_t i, len = 0, iL;

	for(i = 0; i < bb->numInstr; i++){

		iL = bbPrvInstrExport(bb->instrs + i, buf, pc + len);
		len += iL;
		if(buf) buf += iL;
	}

	return len;
}

void bbFinalizeChanges(){

	LLN* n;
	uint32_t i, pc = 0, len = 0;

	//recalc lengths and addresses of blocks
	n = bbList.next;
	while(n != &bbList){

		BB* bb = (BB*)n;
		n = n->next;

		bb->addr = len;
		bb->len = bbPrvExportOneBlock(bb, NULL, len);
		len += bb->len;
	}

	//calculate final offsets for branches and switches
	pc = 0;
	n = bbList.next;
	while(n != &bbList){

		BB* bb = (BB*)n;
		n = n->next;

		for(i = 0; i < bb->numInstr; i++){

			if(bb->instrs[i].destLen){	//branch

				bb->instrs[i].finalOffset = bb->instrs[i].jmpDest->addr - pc;

				if(bb->instrs[i].destLen == 2){	//check offset fits

					int32_t t = bb->instrs[i].finalOffset >> 15;

					if(t && t != -1){

						fprintf(stderr, "offsett impossible in 16 bits: %" PRIi32 "\n", bb->instrs[i].finalOffset);
						exit(-23);
					}
				}
			}
			else if(bb->instrs[i].type == INSTR_TYPE_TABLESWITCH || bb->instrs[i].type == INSTR_TYPE_LOOKUPSWITCH){

				uint32_t j;

				for(j = 0; j <= bb->instrs[i].switches.numCases; j++){

					bb->instrs[i].switches.destOffsets[j] = bb->instrs[i].switches.dests[j]->addr - pc;
				}
			}

			pc += bbPrvInstrExport(bb->instrs + i, NULL, pc);
		}
	}
}

//export
uint32_t bbExport(uint8_t* buf){

	LLN* n;
	uint32_t bL, len = 0;

	n = bbList.next;
	while(n != &bbList){

		bL = bbPrvExportOneBlock((BB*)n, buf, len);
		len += bL;
		if(buf) buf += bL;
		n = n->next;
	}

	return len;
}

void bbGetExc(uint32_t idx, uint16_t* startP, uint16_t* endP, uint16_t* handlerP){

	LLN* n;
	uint32_t idxOrig = idx;

	n = excList.next;
	while(n != &excList){

		ExcEntry* ee = (ExcEntry*)n;

		n = n->next;

		if(idx) idx--;
		else {

			if((ee->start->addr & 0xFFFF0000) || (ee->end->addr & 0xFFFF0000) || (ee->handler->addr & 0xFFFF0000)){

				fprintf(stderr, "Exception pointer out of range for exception %" PRIu32 " (0x%06" PRIX32 " 0x%06" PRIX32 " 0x%06" PRIX32 ")\n",
						idxOrig, ee->start->addr, ee->end->addr, ee->handler->addr);
				exit(-22);
				/*
					in theory, we could be clever, shuffle some basic blocks around to make this work anyways.
					We [currently] have no interest in that theory due to its complexity and relatively low payoff.
				*/
			}

			*startP = ee->start->addr;
			*endP = ee->end->addr;
			*handlerP = ee->handler->addr;
			return;
		}
	}

	//we asked too much
	fprintf(stderr, "Asked for nonexistent exception handler (index %" PRIu32 ")\n", idxOrig);
	exit(-18);
}


//cleanup
void bbDestroy(){

	LLN* n;

	//free basic blocks
	n = bbList.next;
	while(n != &bbList){

		BB* bb = (BB*)n;
		uint32_t i;

		n = n->next;

		for(i = 0; i < bb->numInstr; i++){

			bbPrvInstrFree(bb->instrs + i);
		}
		mfree(bb);
	}

	//free exception table
	n = excList.next;
	while(n != &excList){

		ExcEntry* ee = (ExcEntry*)n;

		n = n->next;

		mfree(ee);
	}
}
