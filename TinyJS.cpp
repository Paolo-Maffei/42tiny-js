/*
 * TinyJS
 *
 * A single-file Javascript-alike engine
 *
 * Authored By Gordon Williams <gw@pur3.co.uk>
 *
 * Copyright (C) 2009 Pur3 Ltd
 *

 * 42TinyJS
 *
 * A fork of TinyJS with the goal to makes a more JavaScript/ECMA compliant engine
 *
 * Authored / Changed By Armin Diedering <armin@diedering.de>
 *
 * Copyright (C) 2010 ardisoft
 *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifdef _DEBUG
#	ifndef _MSC_VER
#		define DEBUG_MEMORY 1
#	endif
#endif
#include <cassert>
#include <cstring>
#include <sstream>
#include <cstdlib>
#include <cstdio>
#include <climits>
#include <cmath>
#include <memory> // for auto_ptr
#include <algorithm>

#include "TinyJS.h"

#ifndef ASSERT
#	define ASSERT(X) assert(X)
#endif
#if 0
inline CScriptVarSmartLink &CScriptVarSmartLink::operator = (CScriptVarPtr Var) {
	if(link && !link->isOwned()) delete link; 
	link = new CScriptVarLink(Var);
/*
	if (!link || link->isOwned()) 
		link = new CScriptVarLink(Var);
	else {
		link->replaceWith(Var);
		link->name=TINYJS_TEMP_NAME;
	} 
*/
	return *this;
}
inline CScriptVarSmartLink &CScriptVarSmartLink::operator = (const CScriptVarSmartLink &Link) { 
	if(link && !link->isOwned()) delete link; 
	link = Link.link; 
	((CScriptVarSmartLink &)Link).link = 0; // explicit cast to a non const ref
	return *this;
}

// this operator corresponds "CLEAN(link); link = Link;"
inline CScriptVarSmartLink &CScriptVarSmartLink::operator = (CScriptVarLink *Link) {
	if(link && !link->isOwned()) delete link; 
	link = Link; 
	return *this;
}
// this operator corresponds "link->replaceWith(Link->get())"
inline CScriptVarSmartLink &CScriptVarSmartLink::operator <<(CScriptVarSmartLink &Link) {
	ASSERT(link && Link.link);
	link->replaceWith(Link.link);
	return *this;
}
#endif

using namespace std;

#ifdef __GNUC__
#	define vsprintf_s vsnprintf
#	define vsnprintf_s(buffer, size, len, format, args) vsnprintf(buffer, size, format, args)
#	define sprintf_s snprintf
#	define _strdup strdup
#else
#	define __attribute__(x)
#endif

// ----------------------------------------------------------------------------------- Memory Debug

//#define DEBUG_MEMORY 1

#if DEBUG_MEMORY

vector<CScriptVar*> allocatedVars;
vector<CScriptVarLink*> allocatedLinks;

void mark_allocated(CScriptVar *v) {
	allocatedVars.push_back(v);
}

void mark_deallocated(CScriptVar *v) {
	for (size_t i=0;i<allocatedVars.size();i++) {
		if (allocatedVars[i] == v) {
			allocatedVars.erase(allocatedVars.begin()+i);
			break;
		}
	}
}

void mark_allocated(CScriptVarLink *v) {
	allocatedLinks.push_back(v);
}

void mark_deallocated(CScriptVarLink *v) {
	for (size_t i=0;i<allocatedLinks.size();i++) {
		if (allocatedLinks[i] == v) {
			allocatedLinks.erase(allocatedLinks.begin()+i);
			break;
		}
	}
}

void show_allocated() {
	for (size_t i=0;i<allocatedVars.size();i++) {
		printf("ALLOCATED, %d refs\n", allocatedVars[i]->getRefs());
		allocatedVars[i]->trace("  ");
	}
	for (size_t i=0;i<allocatedLinks.size();i++) {
		printf("ALLOCATED LINK %s, allocated[%d] to \n", allocatedLinks[i]->getName().c_str(), (*allocatedLinks[i])->getRefs());
		(*allocatedLinks[i])->trace("  ");
	}
	allocatedVars.clear();
	allocatedLinks.clear();
}
#endif

// ----------------------------------------------------------------------------------- Utils
bool isWhitespace(char ch) {
	return (ch==' ') || (ch=='\t') || (ch=='\n') || (ch=='\r');
}

bool isNumeric(char ch) {
	return (ch>='0') && (ch<='9');
}
bool isNumber(const string &str) {
	for (size_t i=0;i<str.size();i++)
		if (!isNumeric(str[i])) return false;
	return true;
}
bool isHexadecimal(char ch) {
	return ((ch>='0') && (ch<='9')) || ((ch>='a') && (ch<='f')) || ((ch>='A') && (ch<='F'));
}
bool isOctal(char ch) {
	return ((ch>='0') && (ch<='7'));
}
bool isAlpha(char ch) {
	return ((ch>='a') && (ch<='z')) || ((ch>='A') && (ch<='Z')) || ch=='_';
}

bool isIDString(const char *s) {
	if (!isAlpha(*s))
		return false;
	while (*s) {
		if (!(isAlpha(*s) || isNumeric(*s)))
			return false;
		s++;
	}
	return true;
}

void replace(string &str, char textFrom, const char *textTo) {
	int sLen = strlen(textTo);
	size_t p = str.find(textFrom);
	while (p != string::npos) {
		str = str.substr(0, p) + textTo + str.substr(p+1);
		p = str.find(textFrom, p+sLen);
	}
}
string int2string(int intData) {
	char buffer[32];
	sprintf_s(buffer, sizeof(buffer), "%d", intData);
	return buffer;
}
string float2string(const double &floatData) {
	char buffer[32];
	sprintf_s(buffer, sizeof(buffer), "%f", floatData);
	return buffer;
}
/// convert the given string into a quoted string suitable for javascript
string getJSString(const string &str) {
	string nStr = str;
	for (size_t i=0;i<nStr.size();i++) {
		const char *replaceWith = "";
		bool replace = true;

		switch (nStr[i]) {
			case '\\': replaceWith = "\\\\"; break;
			case '\n': replaceWith = "\\n"; break;
			case '\r': replaceWith = "\\r"; break;
			case '\a': replaceWith = "\\a"; break;
			case '\b': replaceWith = "\\b"; break;
			case '\f': replaceWith = "\\f"; break;
			case '\t': replaceWith = "\\t"; break;
			case '\v': replaceWith = "\\v"; break;
			case '"': replaceWith = "\\\""; break;
//			case '\'': replaceWith = "\\'"; break;
			default: {
					int nCh = ((int)nStr[i]) & 0xff;
					if(nCh<32 || nCh>127) {
						char buffer[5];
						sprintf_s(buffer, sizeof(buffer), "\\x%02X", nCh);
						replaceWith = buffer;
					} else replace=false;
				}
		}

		if (replace) {
			nStr = nStr.substr(0, i) + replaceWith + nStr.substr(i+1);
			i += strlen(replaceWith)-1;
		}
	}
	return "\"" + nStr + "\"";
}

/** Is the string alphanumeric */
bool isAlphaNum(const string &str) {
	if (str.size()==0) return true;
	if (!isAlpha(str[0])) return false;
	for (size_t i=0;i<str.size();i++)
		if (!(isAlpha(str[i]) || isNumeric(str[i])))
			return false;
	return true;
}

// ----------------------------------------------------------------------------------- CSCRIPTEXCEPTION
/*
CScriptException::CScriptException(const string &exceptionText, int Pos) {
	text = exceptionText;
	pos = Pos;
}
*/
// ----------------------------------------------------------------------------------- CSCRIPTLEX

CScriptLex::CScriptLex(const char *Code, const string &File, int Line, int Column) : data(Code) {
	currentFile = File;
	currentLineStart = tokenStart = data;
	reset(data, Line, data);
}


void CScriptLex::reset(const char *toPos, int Line, const char *LineStart) {
	dataPos = toPos;
	tokenStart = data;
	tk = last_tk = 0;
	tkStr = "";
	currentLine = Line;
	currentLineStart = LineStart;
	currCh = nextCh = 0;
	getNextCh(); // currCh
	getNextCh(); // nextCh
	getNextToken();
}

void CScriptLex::check(int expected_tk) {
	if (expected_tk==';' && tk==LEX_EOF) return; // ignore last missing ';'
	if (tk!=expected_tk) {
		ostringstream errorString;
		if(expected_tk == LEX_EOF)
			errorString << "Got unexpected " << getTokenStr(tk);
		else
			errorString << "Got " << getTokenStr(tk) << " expected " << getTokenStr(expected_tk);
		throw new CScriptException(errorString.str(), currentFile, currentLine, currentColumn());
	}
}
void CScriptLex::match(int expected_tk) {
	check(expected_tk);
	getNextToken();
}

string CScriptLex::getTokenStr(int token) {
	if (token>32 && token<128) {
		//			char buf[4] = "' '";
		//			buf[1] = (char)token;
			char buf[2] = " ";
			buf[0] = (char)token;
			return buf;
	}
	switch (token) {
		case LEX_EOF : return "EOF";
		case LEX_ID : return "ID";
		case LEX_INT : return "INT";
		case LEX_FLOAT : return "FLOAT";
		case LEX_STR : return "STRING";
		case LEX_REGEXP : return "REGEXP";
		case LEX_EQUAL : return "==";
		case LEX_TYPEEQUAL : return "===";
		case LEX_NEQUAL : return "!=";
		case LEX_NTYPEEQUAL : return "!==";
		case LEX_LEQUAL : return "<=";
		case LEX_LSHIFT : return "<<";
		case LEX_LSHIFTEQUAL : return "<<=";
		case LEX_GEQUAL : return ">=";
		case LEX_RSHIFT : return ">>";
		case LEX_RSHIFTEQUAL : return ">>=";
		case LEX_RSHIFTU : return ">>>";
		case LEX_RSHIFTUEQUAL : return ">>>=";
		case LEX_PLUSEQUAL : return "+=";
		case LEX_MINUSEQUAL : return "-=";
		case LEX_PLUSPLUS : return "++";
		case LEX_MINUSMINUS : return "--";
		case LEX_ANDEQUAL : return "&=";
		case LEX_ANDAND : return "&&";
		case LEX_OREQUAL : return "|=";
		case LEX_OROR : return "||";
		case LEX_XOREQUAL : return "^=";
		case LEX_ASTERISKEQUAL : return "*=";
		case LEX_SLASHEQUAL : return "/=";
		case LEX_PERCENTEQUAL : return "%=";

		// reserved words
		case LEX_R_IF : return "if";
		case LEX_R_ELSE : return "else";
		case LEX_R_DO : return "do";
		case LEX_R_WHILE : return "while";
		case LEX_R_FOR : return "for";
		case LEX_T_FOR_IN : return "for";
		case LEX_T_FOR_EACH_IN : return "for each";
		case LEX_R_IN: return "in";
		case LEX_R_BREAK : return "break";
		case LEX_R_CONTINUE : return "continue";
		case LEX_R_FUNCTION : return "function";
		case LEX_T_FUNCTION_OPERATOR : return "function";
		case LEX_T_GET : return "get";
		case LEX_T_SET : return "set";

		case LEX_R_RETURN : return "return";
		case LEX_R_VAR : return "var";
		case LEX_R_LET : return "let";
		case LEX_R_WITH : return "with";
		case LEX_R_TRUE : return "true";
		case LEX_R_FALSE : return "false";
		case LEX_R_NULL : return "null";
		case LEX_R_NEW : return "new";
		case LEX_R_TRY : return "try";
		case LEX_R_CATCH : return "catch";
		case LEX_R_FINALLY : return "finally";
		case LEX_R_THROW : return "throw";
		case LEX_R_TYPEOF : return "typeof";
		case LEX_R_VOID : return "void";
		case LEX_R_DELETE : return "delete";
		case LEX_R_INSTANCEOF : return "instanceof";
		case LEX_R_SWITCH : return "switch";
		case LEX_R_CASE : return "case";
		case LEX_R_DEFAULT : return "default";

		case LEX_T_SKIP : return "LEX_SKIP";
		case LEX_T_LABEL: return "LABEL";
	}

	ostringstream msg;
	msg << "?[" << token << "]";
	return msg.str();
}

void CScriptLex::getNextCh() {
	if(currCh == '\n') { // Windows or Linux
		currentLine++;
		tokenStart = currentLineStart = dataPos - (nextCh == LEX_EOF ?  0 : 1);
	}
	currCh = nextCh;
	if ( (nextCh = *dataPos) != LEX_EOF ) dataPos++; // stay on EOF
	if(currCh == '\r') { // Windows or Mac
		if(nextCh == '\n')
			getNextCh(); // Windows '\r\n\' --> skip '\r'
		else
			currCh = '\n'; // Mac (only '\r') --> convert '\r' to '\n'
	}
}
static uint16_t not_allowed_tokens_befor_regexp[] = {LEX_ID, LEX_INT, LEX_FLOAT, LEX_STR, LEX_R_TRUE, LEX_R_FALSE, LEX_R_NULL, ']', ')', '.', LEX_EOF};
void CScriptLex::getNextToken() {
	while (currCh && isWhitespace(currCh)) getNextCh();
	// newline comments
	if (currCh=='/' && nextCh=='/') {
			while (currCh && currCh!='\n') getNextCh();
			getNextCh();
			getNextToken();
			return;
	}
	// block comments
	if (currCh=='/' && nextCh=='*') {
			while (currCh && (currCh!='*' || nextCh!='/')) getNextCh();
			getNextCh();
			getNextCh();
			getNextToken();
			return;
	}
	last_tk = tk;
	tk = LEX_EOF;
	tkStr.clear();
	// record beginning of this token
	tokenStart = dataPos - (nextCh == LEX_EOF ? (currCh == LEX_EOF ? 0 : 1) : 2);
	// tokens
	if (isAlpha(currCh)) { //  IDs
		while (isAlpha(currCh) || isNumeric(currCh)) {
			tkStr += currCh;
			getNextCh();
		}
		tk = LEX_ID;
			  if (tkStr=="if")			tk = LEX_R_IF;
		else if (tkStr=="else")			tk = LEX_R_ELSE;
		else if (tkStr=="do")			tk = LEX_R_DO;
		else if (tkStr=="while")		tk = LEX_R_WHILE;
		else if (tkStr=="for")			tk = LEX_R_FOR;
		else if (tkStr=="in")			tk = LEX_R_IN;
		else if (tkStr=="break")		tk = LEX_R_BREAK;
		else if (tkStr=="continue")	tk = LEX_R_CONTINUE;
		else if (tkStr=="function")	tk = LEX_R_FUNCTION;
		else if (tkStr=="return")		tk = LEX_R_RETURN;
		else if (tkStr=="var")			tk = LEX_R_VAR;
		else if (tkStr=="let")			tk = LEX_R_LET;
		else if (tkStr=="with")			tk = LEX_R_WITH;
		else if (tkStr=="true")			tk = LEX_R_TRUE;
		else if (tkStr=="false")		tk = LEX_R_FALSE;
		else if (tkStr=="null")			tk = LEX_R_NULL;
		else if (tkStr=="new")			tk = LEX_R_NEW;
		else if (tkStr=="try")			tk = LEX_R_TRY;
		else if (tkStr=="catch")		tk = LEX_R_CATCH;
		else if (tkStr=="finally")		tk = LEX_R_FINALLY;
		else if (tkStr=="throw")		tk = LEX_R_THROW;
		else if (tkStr=="typeof")		tk = LEX_R_TYPEOF;
		else if (tkStr=="void")			tk = LEX_R_VOID;
		else if (tkStr=="delete")		tk = LEX_R_DELETE;
		else if (tkStr=="instanceof")	tk = LEX_R_INSTANCEOF;
		else if (tkStr=="switch")		tk = LEX_R_SWITCH;
		else if (tkStr=="case")			tk = LEX_R_CASE;
		else if (tkStr=="default")		tk = LEX_R_DEFAULT;
	} else if (isNumeric(currCh) || (currCh=='.' && isNumeric(nextCh))) { // Numbers
		if(currCh=='.') tkStr+='0';
		bool isHex = false;
		if (currCh=='0') { tkStr += currCh; getNextCh(); }
		if (currCh=='x' || currCh=='X') {
			isHex = true;
			tkStr += currCh; getNextCh();
		}
		tk = LEX_INT;
		while (isNumeric(currCh) || (isHex && isHexadecimal(currCh))) {
			tkStr += currCh;
			getNextCh();
		}
		if (!isHex && currCh=='.') {
			tk = LEX_FLOAT;
			tkStr += '.';
			getNextCh();
			while (isNumeric(currCh)) {
				tkStr += currCh;
				getNextCh();
			}
		}
		// do fancy e-style floating point
		if (!isHex && (currCh=='e' || currCh=='E')) {
			tk = LEX_FLOAT;
			tkStr += currCh; getNextCh();
			if (currCh=='-') { tkStr += currCh; getNextCh(); }
			while (isNumeric(currCh)) {
				tkStr += currCh; getNextCh();
			}
		}
	} else if (currCh=='"' || currCh=='\'') {	// strings...
		char endCh = currCh;
		getNextCh();
		while (currCh && currCh!=endCh && currCh!='\n') {
			if (currCh == '\\') {
				getNextCh();
				switch (currCh) {
					case '\n' : break; // ignore newline after '\'
					case 'n': tkStr += '\n'; break;
					case 'r': tkStr += '\r'; break;
					case 'a': tkStr += '\a'; break;
					case 'b': tkStr += '\b'; break;
					case 'f': tkStr += '\f'; break;
					case 't': tkStr += '\t'; break;
					case 'v': tkStr += '\v'; break;
					case 'x': { // hex digits
						getNextCh();
						char buf[3]="\0\0";
						for(int i=0; i<2 && isHexadecimal(currCh); i++)
						{
							buf[i] = currCh; getNextCh();
						}
						tkStr += (char)strtol(buf, 0, 16);
					}
					default: {
						if(isOctal(currCh)) {
							char buf[4]="\0\0\0";
							for(int i=0; i<3 && isOctal(currCh); i++)
							{
								buf[i] = currCh; getNextCh();
							}
							tkStr += (char)strtol(buf, 0, 8);
						}
						else tkStr += currCh;
					}
				}
			} else {
				tkStr += currCh;
			}
			getNextCh();
		}
		if(currCh != endCh)
			throw new CScriptException("unterminated string literal", currentFile, currentLine, currentColumn());
		getNextCh();
		tk = LEX_STR;
	} else {
		// single chars
		tk = currCh;
		if (currCh) getNextCh();
		if (tk=='=' && currCh=='=') { // ==
			tk = LEX_EQUAL;
			getNextCh();
			if (currCh=='=') { // ===
				tk = LEX_TYPEEQUAL;
				getNextCh();
			}
		} else if (tk=='!' && currCh=='=') { // !=
			tk = LEX_NEQUAL;
			getNextCh();
			if (currCh=='=') { // !==
				tk = LEX_NTYPEEQUAL;
				getNextCh();
			}
		} else if (tk=='<') {
			if (currCh=='=') {	// <=
				tk = LEX_LEQUAL;
				getNextCh();
			} else if (currCh=='<') {	// <<
				tk = LEX_LSHIFT;
				getNextCh();
				if (currCh=='=') { // <<=
					tk = LEX_LSHIFTEQUAL;
					getNextCh();
				}
			}
		} else if (tk=='>') {
			if (currCh=='=') {	// >=
				tk = LEX_GEQUAL;
				getNextCh();
			} else if (currCh=='>') {	// >>
				tk = LEX_RSHIFT;
				getNextCh();
				if (currCh=='=') { // >>=
					tk = LEX_RSHIFTEQUAL;
					getNextCh();
				} else if (currCh=='>') { // >>>
					tk = LEX_RSHIFTU;
					getNextCh();
					if (currCh=='=') { // >>>=
						tk = LEX_RSHIFTUEQUAL;
						getNextCh();
					}				
				}
			}
		}  else if (tk=='+') {
			if (currCh=='=') {	// +=
				tk = LEX_PLUSEQUAL;
				getNextCh();
			}  else if (currCh=='+') {	// ++
				tk = LEX_PLUSPLUS;
				getNextCh();
			}
		}  else if (tk=='-') {
			if (currCh=='=') {	// -=
				tk = LEX_MINUSEQUAL;
				getNextCh();
			}  else if (currCh=='-') {	// --
				tk = LEX_MINUSMINUS;
				getNextCh();
			}
		} else if (tk=='&') {
			if (currCh=='=') {			// &=
				tk = LEX_ANDEQUAL;
				getNextCh();
			} else if (currCh=='&') {	// &&
				tk = LEX_ANDAND;
				getNextCh();
			}
		} else if (tk=='|') {
			if (currCh=='=') {			// |=
				tk = LEX_OREQUAL;
				getNextCh();
			} else if (currCh=='|') {	// ||
				tk = LEX_OROR;
				getNextCh();
			}
		} else if (tk=='^' && currCh=='=') {
			tk = LEX_XOREQUAL;
			getNextCh();
		} else if (tk=='*' && currCh=='=') {
			tk = LEX_ASTERISKEQUAL;
			getNextCh();
		} else if (tk=='/') {
			// check if it's a Regex-Literal
			tk = LEX_REGEXP;
			for(uint16_t *p = not_allowed_tokens_befor_regexp; *p; p++) {
				if(*p==last_tk) { tk = '/'; break; }
			}
			if(tk == LEX_REGEXP) {
				tkStr = "/";
				while (currCh && currCh!='/' && currCh!='\n') {
					if (currCh == '\\' && nextCh == '/') {
						tkStr.append(1, currCh);
						getNextCh();
					}
					tkStr.append(1, currCh);
					getNextCh();
				}
				if(currCh == '/') {
					do {
						tkStr.append(1, currCh);
						getNextCh();
					} while (currCh && currCh=='g' && currCh=='i' && currCh=='m' && currCh=='y');
				} else
					throw new CScriptException("unterminated regular expression literal", currentFile, currentLine, currentColumn());
			} else if(currCh=='=') {
				tk = LEX_SLASHEQUAL;
				getNextCh();
			}
		} else if (tk=='%' && currCh=='=') {
			tk = LEX_PERCENTEQUAL;
			getNextCh();
		}
	}
	/* This isn't quite right yet */
}

// ----------------------------------------------------------------------------------- CSCRIPTTOKEN
CScriptToken::CScriptToken(CScriptLex *l, int Match) : line(l->currentLine), column(l->currentColumn()), token(l->tk), intData(0)
{
	if(token == LEX_INT)
		intData = strtol(l->tkStr.c_str(),0,0);
	else if(LEX_TOKEN_DATA_FLOAT(token))
		floatData = new double(strtod(l->tkStr.c_str(),0));
	else if(LEX_TOKEN_DATA_STRING(token)) {
		stringData = new CScriptTokenDataString(l->tkStr);
		stringData->ref();
	} else if(LEX_TOKEN_DATA_FUNCTION(token)) {
		fncData = new CScriptTokenDataFnc;
		fncData->ref();
	}
	if(Match>=0)
		l->match(Match);
	else
		l->match(l->tk); 
}
CScriptToken::CScriptToken(uint16_t Tk, int IntData) : line(0), column(0), token(Tk) {
	if (LEX_TOKEN_DATA_SIMPLE(token)) {
		intData = IntData;
	} else if (LEX_TOKEN_DATA_FUNCTION(token)) {
		fncData = new CScriptTokenDataFnc;
		fncData->ref();
	} else 
		ASSERT(0);
}


CScriptToken::CScriptToken(uint16_t Tk, const string &TkStr) : line(0), column(0), token(Tk) {
	ASSERT(LEX_TOKEN_DATA_STRING(token));
	stringData = new CScriptTokenDataString(TkStr);
	stringData->ref();
}

CScriptToken &CScriptToken::operator =(const CScriptToken &Copy)
{
	clear();
	line			= Copy.line;
	column		= Copy.column; 
	token			= Copy.token;
	if(LEX_TOKEN_DATA_FLOAT(token))
		floatData = new double(*Copy.floatData);
	else if(LEX_TOKEN_DATA_STRING(token)) {
		stringData = Copy.stringData;
		stringData->ref();
	} else if(LEX_TOKEN_DATA_FUNCTION(token)) {
		fncData = Copy.fncData;
		fncData->ref();
	} else
		intData	= Copy.intData;
	return *this;
}
string CScriptToken::getParsableString(string &indentString, int &newln, const string &indent) {
	string OutString;
	string nl = indent.size() ? "\n" : " ";
	int last_newln = newln;
	if(newln&2) OutString.append(nl);
	if(newln&1) OutString.append(indentString);
	newln = 0;

	if(LEX_TOKEN_DATA_STRING(token))
		OutString.append(String()).append(" ");
	else if(LEX_TOKEN_DATA_FLOAT(token))
		OutString.append(float2string(Float())).append(" ");
	else if(token == LEX_INT)
		OutString.append(int2string(Int())).append(" ");
	else if(LEX_TOKEN_DATA_FUNCTION(token)) {
		OutString.append("function ");
		if(token == LEX_R_FUNCTION)
			OutString.append(Fnc().name);
		OutString.append("(");
		if(Fnc().arguments.size()) {
			OutString.append(Fnc().arguments.front());
			for(vector<string>::iterator it=Fnc().arguments.begin()+1; it!=Fnc().arguments.end(); ++it)
				OutString.append(", ").append(*it);
		}
		OutString.append(") ");
		for(TOKEN_VECT::iterator it=Fnc().body.begin(); it != Fnc().body.end(); ++it) {
			OutString.append(it->getParsableString(indentString, newln, indent));
		}
	} else if(token == '{') {
		OutString.append("{");
		indentString.append(indent);
		newln = 1|2|4;
	} else if(token == '}') {
		if(last_newln==(1|2|4)) OutString =  "";
		indentString.resize(indentString.size() - min(indentString.size(),indent.size()));
		OutString.append("}");
		newln = 1|2;
	} else {
		OutString.append(CScriptLex::getTokenStr(token));
		if(token==';') newln=1|2; else OutString.append(" ");
	}
	return OutString;
}
string CScriptToken::getParsableString(const string indent) {
	string indentString;
	int newln = 0;
	return getParsableString(indentString, newln, indent);
}
void CScriptToken::print(string &indent )
{
	if(LEX_TOKEN_DATA_STRING(token))
		printf("%s%s ", indent.c_str(), String().c_str());
	else if(LEX_TOKEN_DATA_FLOAT(token))
		printf("%s%f ", indent.c_str(), Float());
	else if(token == LEX_INT)
		printf("%s%d ", indent.c_str(), Int());
	else if(LEX_TOKEN_DATA_FUNCTION(token)) {
		if(token == LEX_R_FUNCTION)
			printf("%sfunction %s(", indent.c_str(), Fnc().name.c_str());
		else 
			printf("%sfunction (", indent.c_str());
		if(Fnc().arguments.size()) {
			printf("%s", Fnc().arguments.front().c_str());
			for(vector<string>::iterator it=Fnc().arguments.begin()+1; it!=Fnc().arguments.end(); ++it)
				printf(",%s", it->c_str());
		}
		printf(")");
		for(TOKEN_VECT::iterator it=Fnc().body.begin(); it != Fnc().body.end(); ++it)
			it->print (indent);
	} else if(token == '{') {
		printf("%s{\n", indent.c_str());
		indent += "   ";
	} else if(token == '}') {
		indent.resize(indent.size()-3);
		printf("}\n");
	} else {
		printf("%s%s ", indent.c_str(), CScriptLex::getTokenStr(token).c_str());
		if(token==';') printf("\n");
	}
}
void CScriptToken::clear()
{
	if(LEX_TOKEN_DATA_STRING(token))
		stringData->unref();
	else if(LEX_TOKEN_DATA_FLOAT(token))
		delete floatData;
	else if(LEX_TOKEN_DATA_FUNCTION(token))
		fncData->unref();
	token = 0;
}

// ----------------------------------------------------------------------------------- CSCRIPTTOKENIZER
CScriptTokenizer::CScriptTokenizer() : l(0), prevPos(&tokens) {
}
CScriptTokenizer::CScriptTokenizer(CScriptLex &Lexer) : l(0), prevPos(&tokens) {
	tokenizeCode(Lexer);
}
CScriptTokenizer::CScriptTokenizer(const char *Code, const string &File, int Line, int Column) : l(0), prevPos(&tokens) {
	CScriptLex lexer(Code, File, Line, Column);
	tokenizeCode(lexer);
}
void CScriptTokenizer::tokenizeCode(CScriptLex &Lexer) {
	try {
		l=&Lexer;
		tokens.clear();
		tokenScopeStack.clear();
		vector<int> blockStart(1, tokens.size()), marks;
		bool statement = true;
		if(l->tk == '�') { // special-Token at Start means the code begins not at Statement-Level
			l->match('�');
			statement = false;
		}
		do {
			tokenizeToken(tokens, statement, blockStart, marks);
		} while (l->tk!=LEX_EOF);
		pushToken(tokens); // add LEX_EOF-Token
		TOKEN_VECT(tokens).swap(tokens);//	tokens.shrink_to_fit();
		pushTokenScope(tokens);
		currentFile = l->currentFile;
		tk = getToken().token;
	} catch (CScriptException *e) {
		ostringstream msg;
		msg << "Error " << e->text;
		if(e->line >= 0) msg << " at Line:" << e->line+1 << " Column:" << e->column+1;
		if(e->file.length()) msg << " in " << e->file;
		delete e;
		l=0;
		throw new CScriptException(msg.str(),"");
	}
}

void CScriptTokenizer::getNextToken() {
	prevPos = tokenScopeStack.back();
	if(getToken().token == LEX_EOF) 
		return;
	ScriptTokenPosition &_TokenPos = tokenScopeStack.back();
	_TokenPos.pos++;
	if(_TokenPos.pos == _TokenPos.tokens->end())
		tokenScopeStack.pop_back();
//	ScriptTokenPosition &TokenPos = tokenScopeStack.back();
	tk = getToken().token;
}



void CScriptTokenizer::match(int ExpectedToken) {
	if(check(ExpectedToken))
		getNextToken();
}
bool CScriptTokenizer::check(int ExpectedToken) {
	int currentToken = getToken().token;
	if (ExpectedToken==';' && (currentToken==LEX_EOF || currentToken=='}')) return false; // ignore last missing ';'
	if (currentToken!=ExpectedToken) {
		ostringstream errorString;
		if(ExpectedToken == LEX_EOF)
			errorString << "Got unexpected " << CScriptLex::getTokenStr(currentToken);
		else
			errorString << "Got " << CScriptLex::getTokenStr(currentToken) << " expected " << CScriptLex::getTokenStr(ExpectedToken);
		throw new CScriptException(errorString.str(), currentFile, currentLine(), currentColumn());
	}
	return true;
}
void CScriptTokenizer::pushTokenScope(TOKEN_VECT &Tokens) {
	tokenScopeStack.push_back(ScriptTokenPosition(&Tokens));
	tk = getToken().token;
}

void CScriptTokenizer::setPos(ScriptTokenPosition &TokenPos) {
	ASSERT( TokenPos.tokens == tokenScopeStack.back().tokens);
	tokenScopeStack.back().pos = TokenPos.pos;
	tk = getToken().token;
}
void CScriptTokenizer::skip(int Tokens) {
	tokenScopeStack.back().pos+=Tokens;
	tk = getToken().token;
}
void CScriptTokenizer::tokenizeCatch(TOKEN_VECT &Tokens, bool &Statement, vector<int> &BlockStart, vector<int> &Marks) {
	if(!Statement)	throwTokenNotExpected();
	Marks.push_back(pushToken(Tokens, LEX_R_CATCH)); // push Token & push tokenBeginIdx
	pushToken(Tokens, '(');
	pushToken(Tokens, LEX_ID);
	if(l->tk == LEX_R_IF) {
		pushToken(Tokens);
		Statement = false;
		while(l->tk != ')' && l->tk != LEX_EOF)
			tokenizeToken(Tokens, Statement, BlockStart, Marks);
	}
	pushToken(Tokens, ')');
	Statement = true;
	l->check('{');
	//	BlockStart.push_back(Tokens.size()); // set a blockStart
	tokenizeBlock(Tokens, Statement, BlockStart, Marks);
	//	BlockStart.pop_back();

	int tokenBeginIdx = Marks.back();
	Marks.pop_back(); // clean-up Marks
	Tokens[tokenBeginIdx].Int() = Tokens.size()-tokenBeginIdx;
}
void CScriptTokenizer::tokenizeTry(TOKEN_VECT &Tokens, bool &Statement, vector<int> &BlockStart, vector<int> &Marks) {
	if(!Statement)	throwTokenNotExpected();
	bool isTry = l->tk == LEX_R_TRY;
	Marks.push_back(pushToken(Tokens)); // push Token & push tokenBeginIdx
	l->check('{');
	//	BlockStart.push_back(Tokens.size()); // set a blockStart
	tokenizeBlock(Tokens, Statement, BlockStart, Marks);
	//	BlockStart.pop_back();

	int tokenBeginIdx = Marks.back();
	Marks.pop_back(); // clean-up Marks
	Tokens[tokenBeginIdx].Int() = Tokens.size()-tokenBeginIdx;

	while(l->tk != LEX_R_FINALLY && isTry) {
		tokenizeCatch(Tokens, Statement, BlockStart, Marks);
	}
	if(l->tk == LEX_R_FINALLY && isTry)
		tokenizeTry(Tokens, Statement, BlockStart, Marks);
}
static inline void setTokenSkip(TOKEN_VECT &Tokens, vector<int> &Marks) {
	int tokenBeginIdx = Marks.back();
	Marks.pop_back();
	Tokens[tokenBeginIdx].Int() = Tokens.size()-tokenBeginIdx;
}
void CScriptTokenizer::tokenizeSwitch(TOKEN_VECT &Tokens, bool &Statement, vector<int> &BlockStart, vector<int> &Marks) {
	if(!Statement)	throwTokenNotExpected();

	Marks.push_back(pushToken(Tokens)); // push Token & push tokenBeginIdx

	Statement = false;
	l->check('(');
	tokenizeBlock(Tokens, Statement, BlockStart, Marks);

	Statement = true;
	Marks.push_back(pushToken(Tokens, '{')); // push Token & push blockBeginIdx
	BlockStart.push_back(Tokens.size());	// push Block-Start (one Token after '{')

	for(bool hasDefault=false;;) {
		if( l->tk == LEX_R_CASE || l->tk == LEX_R_DEFAULT) {
			if(l->tk == LEX_R_CASE) {
				Marks.push_back(pushToken(Tokens)); // push Token & push caseBeginIdx
				Statement = false;
				while(l->tk != ':' && l->tk != LEX_EOF )
					tokenizeToken(Tokens, Statement, BlockStart, Marks); 
				setTokenSkip(Tokens, Marks);
			} else { // default
				if(hasDefault) throw new CScriptException("more than one switch default", l->currentFile, l->currentLine, l->currentColumn());
				hasDefault = true;
				pushToken(Tokens);
			}
			
			Marks.push_back(pushToken(Tokens, ':'));
			Statement = true;
			while(l->tk != '}' && l->tk != LEX_R_CASE && l->tk != LEX_R_DEFAULT && l->tk != LEX_EOF )
				tokenizeStatement(Tokens, Statement, BlockStart, Marks); 
			setTokenSkip(Tokens, Marks);
		} else if(l->tk == '}') {
			break;
		} else
			throw new CScriptException("invalid switch statement", l->currentFile, l->currentLine, l->currentColumn());
	}
	pushToken(Tokens, '}');
	BlockStart.pop_back();
	Statement = true;

	int tokenBeginIdx = Marks.back(); // get block begin
	Marks.pop_back(); // clean-up Marks
	Tokens[tokenBeginIdx].Int() = Tokens.size()-tokenBeginIdx;

	tokenBeginIdx = Marks.back(); // get switch gegin
	Marks.pop_back(); // clean-up Marks
	Tokens[tokenBeginIdx].Int() = Tokens.size()-tokenBeginIdx;
}
void CScriptTokenizer::tokenizeWith(TOKEN_VECT &Tokens, bool &Statement, vector<int> &BlockStart, vector<int> &Marks) {
	if(!Statement)	throwTokenNotExpected();

	Marks.push_back(pushToken(Tokens)); // push Token & push tokenBeginIdx

	Statement = false;
	l->check('(');
	tokenizeBlock(Tokens, Statement, BlockStart, Marks);
	Statement = true;

	BlockStart.push_back(Tokens.size()); // set a blockStart
	tokenizeStatement(Tokens, Statement, BlockStart, Marks);
	BlockStart.pop_back();

	int tokenBeginIdx = Marks.back();
	Marks.pop_back(); // clean-up Marks
	Tokens[tokenBeginIdx].Int() = Tokens.size()-tokenBeginIdx;
}
void CScriptTokenizer::tokenizeWhile(TOKEN_VECT &Tokens, bool &Statement, vector<int> &BlockStart, vector<int> &Marks) {
	if(!Statement)	throwTokenNotExpected();

	Marks.push_back(pushToken(Tokens)); // push Token & push tokenBeginIdx
	Statement = false;
	l->check('(');
	tokenizeBlock(Tokens, Statement, BlockStart, Marks);

	Marks.push_back(Tokens.size()); // push skiperBeginIdx
	Tokens.push_back(CScriptToken(LEX_T_SKIP)); // skip 

	Statement = true;

	BlockStart.push_back(Tokens.size()); // set a blockStart
	tokenizeStatement(Tokens, Statement, BlockStart, Marks);
	BlockStart.pop_back();

	int tokenBeginIdx = Marks.back(); // set skipper
	Marks.pop_back(); // clean-up Marks
	Tokens[tokenBeginIdx].Int() = Tokens.size()-tokenBeginIdx;

	tokenBeginIdx = Marks.back();
	Marks.pop_back(); // clean-up Marks
	Tokens[tokenBeginIdx].Int() = Tokens.size()-tokenBeginIdx;
}
void CScriptTokenizer::tokenizeDo(TOKEN_VECT &Tokens, bool &Statement, vector<int> &BlockStart, vector<int> &Marks) {
	if(!Statement)	throwTokenNotExpected();
	Marks.push_back(pushToken(Tokens)); // push Token & push tokenBeginIdx

	BlockStart.push_back(Tokens.size()); // set a blockStart
	tokenizeStatement(Tokens, Statement, BlockStart, Marks);
	BlockStart.pop_back();
	pushToken(Tokens, LEX_R_WHILE);
	Statement = false;
	l->check('(');
	tokenizeBlock(Tokens, Statement, BlockStart, Marks);
	pushToken(Tokens, ';');
	Statement = true;

	int tokenBeginIdx = Marks.back();
	Marks.pop_back(); // clean-up Marks
	Tokens[tokenBeginIdx].Int() = Tokens.size()-tokenBeginIdx;
}

void CScriptTokenizer::tokenizeIf(TOKEN_VECT &Tokens, bool &Statement, vector<int> &BlockStart, vector<int> &Marks) {
	if(!Statement)	throwTokenNotExpected();

	Marks.push_back(pushToken(Tokens)); // push Token & push tokenBeginIdx

	Statement = false;
	l->check('(');
	tokenizeBlock(Tokens, Statement, BlockStart, Marks);

	Marks.push_back(Tokens.size()); // push skiperBeginIdx
	Tokens.push_back(CScriptToken(LEX_T_SKIP)); // skip 

	Statement = true;
	BlockStart.push_back(Tokens.size()); // set a blockStart
	tokenizeStatement(Tokens, Statement, BlockStart, Marks);
	BlockStart.pop_back();

	int tokenBeginIdx = Marks.back(); // set skipper
	Marks.pop_back(); // clean-up Marks
	Tokens[tokenBeginIdx].Int() = Tokens.size()-tokenBeginIdx;

	if(l->tk == LEX_R_ELSE) {
		Marks.push_back(pushToken(Tokens)); // push Token & push tokenBeginIdx
		
		BlockStart.push_back(Tokens.size()); // set a blockStart
		tokenizeStatement(Tokens, Statement, BlockStart, Marks);
		BlockStart.pop_back();

		tokenBeginIdx = Marks.back();
		Marks.pop_back(); // clean-up Marks
		Tokens[tokenBeginIdx].Int() = Tokens.size()-tokenBeginIdx;
	}

	tokenBeginIdx = Marks.back();
	Marks.pop_back(); // clean-up Marks
	Tokens[tokenBeginIdx].Int() = Tokens.size()-tokenBeginIdx;
}

static void fix_BlockStarts_Marks(vector<int> &BlockStart, vector<int> &Marks, int start, int diff) {
	for(vector<int>::iterator it = BlockStart.begin(); it != BlockStart.end(); ++it)
		if(*it >= start) *it += diff;
	for(vector<int>::iterator it = Marks.begin(); it != Marks.end(); ++it)
		if(*it >= start) *it += diff;
}
void CScriptTokenizer::tokenizeFor(TOKEN_VECT &Tokens, bool &Statement, vector<int> &BlockStart, vector<int> &Marks) {
	if(!Statement)	throwTokenNotExpected();

	const char *prev_pos=l->tokenStart;
	const char *prev_line_start=l->currentLineStart;
	int prev_line = l->currentLine;
	bool for_in, for_each_in;
	l->match(LEX_R_FOR);
	if((for_in = for_each_in = (l->tk == LEX_ID && l->tkStr == "each"))) {
		l->match(LEX_ID);
	}
	if(!for_in) {
		l->match('(');
		int parentheses = 0;
		while(l->tk != ')' || parentheses)
		{
			if(l->tk == '(') parentheses++;
			else if(l->tk == ')') parentheses--;
			else if(l->tk == LEX_R_IN) {
				for_in = true;
				break;
			} else if(l->tk==LEX_EOF) {
				l->match(')');
			}
			l->match(l->tk);
		}
	}
	l->reset(prev_pos, prev_line, prev_line_start);


	l->match(LEX_R_FOR);
	if(for_each_in) {
		l->match(LEX_ID);
	}

	Marks.push_back(Tokens.size()); // push tokenBeginIdx
	Tokens.push_back(CScriptToken(for_in?(for_each_in?LEX_T_FOR_EACH_IN:LEX_T_FOR_IN):LEX_R_FOR));

	BlockStart.push_back(pushToken(Tokens, '(')+1); // set BlockStart - no forwarde for(let ...
	Statement = true;
	if(for_in) {
		if(l->tk == LEX_R_VAR) {
			l->match(LEX_R_VAR);

			int tokenInsertIdx = BlockStart.front();
			Tokens.insert(Tokens.begin()+tokenInsertIdx++, CScriptToken(LEX_R_VAR));
			Tokens.insert(Tokens.begin()+tokenInsertIdx++, CScriptToken(LEX_ID, l->tkStr));
			Tokens.insert(Tokens.begin()+tokenInsertIdx++, CScriptToken(';'));
			fix_BlockStarts_Marks(BlockStart, Marks, BlockStart.front(), 3);
		} else if(l->tk == LEX_R_LET) {
			pushToken(Tokens, LEX_R_LET);
		}
		pushToken(Tokens, LEX_ID);
		pushToken(Tokens, LEX_R_IN);
		
	}
	while(l->tk != ')' && l->tk != LEX_EOF ) {
		tokenizeToken(Tokens, Statement, BlockStart, Marks); 
	}
	BlockStart.pop_back(); // pop_back / "no forwarde for(let ... "-prevention
	pushToken(Tokens, ')');
	Statement = true;
	BlockStart.push_back(Tokens.size()); // set a blockStart
	tokenizeStatement(Tokens, Statement, BlockStart, Marks);
	BlockStart.pop_back();

	int tokenBeginIdx = Marks.back();
	Marks.pop_back(); // clean-up Marks
	Tokens[tokenBeginIdx].Int() = Tokens.size()-tokenBeginIdx;
}
void CScriptTokenizer::tokenizeFunction(TOKEN_VECT &Tokens, bool &Statement, vector<int> &BlockStart, vector<int> &Marks) {
	bool forward = false;
	int tk = l->tk;
	if(tk == LEX_ID && (l->tkStr=="get"||l->tkStr=="set")) {
		string tkStr = l->tkStr;
		tk = tkStr=="get"?LEX_T_GET:LEX_T_SET;
		l->match(l->tk);
		if(l->tk != LEX_ID) { // is not a getter or setter 
			tokens.push_back(CScriptToken(LEX_ID, tkStr));
			return;
		}
	} else {
		l->match(LEX_R_FUNCTION);
		if(!Statement) tk = LEX_T_FUNCTION_OPERATOR;
	}
	if(tk == LEX_R_FUNCTION) // only forward functions 
		forward = TOKEN_VECT::size_type(BlockStart.front()) != Tokens.size();

	CScriptToken FncToken(tk);
	CScriptTokenDataFnc &FncData = FncToken.Fnc();
	if(l->tk == LEX_ID) {
		FncData.name = l->tkStr;
		l->match(LEX_ID);
	} else {
		//ASSERT(Statement == false);
	}

	l->match('(');
	while(l->tk != ')') {
		FncData.arguments.push_back(l->tkStr);
		l->match(LEX_ID);
		if (l->tk==',') {
			l->match(','); 
		}
	}
	l->match(')');
	FncData.file = l->currentFile;
	FncData.line = l->currentLine;

	vector<int> functionBlockStart, marks;
	functionBlockStart.push_back(FncData.body.size()+1);
	l->check('{');
	bool FncStatement = true; // functions-block starts always in Statement-Level
	tokenizeBlock(FncData.body, FncStatement, functionBlockStart, marks);

	if(forward) {
		int tokenInsertIdx = BlockStart.back();
		Tokens.insert(Tokens.begin()+BlockStart.front(), FncToken);
		fix_BlockStarts_Marks(BlockStart, Marks, tokenInsertIdx, 1);
	}
	else
		Tokens.push_back(FncToken);
}

void CScriptTokenizer::tokenizeLet(TOKEN_VECT &Tokens, bool &Statement, vector<int> &BlockStart, vector<int> &Marks) {

	bool forward = TOKEN_VECT::size_type(BlockStart.back()) != Tokens.size();
	bool let_is_Statement = Statement, expression=false;

	Marks.push_back(pushToken(Tokens)); // push Token & push BeginIdx
	Statement = false;

	if(l->tk == '(' || !let_is_Statement) {
		expression = true;
		pushToken(Tokens, '(');
	}
	vector<string> vars;
	for(;;) {
		vars.push_back(l->tkStr);
		pushToken(Tokens, LEX_ID);
		if(l->tk=='=') {
			pushToken(Tokens);
			for(;;) {
				if(l->tk == (expression?')':';') || l->tk == ',' || l->tk == LEX_EOF) break;
				tokenizeToken(Tokens, Statement, BlockStart, Marks);
			}
		}
		if(l->tk==',')
			pushToken(Tokens);
		else
			break;
	}
	if(expression) {
		pushToken(Tokens, ')');
		Statement = let_is_Statement;
		if(Statement) 
			tokenizeStatement(Tokens, Statement, BlockStart, Marks);
		int tokenBeginIdx = Marks.back();
		Marks.pop_back(); // clean-up Marks
		Tokens[tokenBeginIdx].Int() = Tokens.size()-tokenBeginIdx;
	} else {
		pushToken(Tokens, ';');
		Statement = true;

		int tokenBeginIdx = Marks.back();
		Marks.pop_back(); // clean-up Marks
		Tokens[tokenBeginIdx].Int() = Tokens.size()-tokenBeginIdx;

		if(forward) // copy a let-deklaration at the begin of the last block
		{
			int tokenInsertIdx = tokenBeginIdx = BlockStart.back();

			Tokens.insert(Tokens.begin()+tokenInsertIdx++, CScriptToken(LEX_R_VAR));
			Tokens.insert(Tokens.begin()+tokenInsertIdx++, CScriptToken(LEX_ID, vars.front()));
			for(vector<string>::iterator it = vars.begin()+1; it != vars.end(); ++it) {
				Tokens.insert(Tokens.begin()+tokenInsertIdx++, CScriptToken(','));
				Tokens.insert(Tokens.begin()+tokenInsertIdx++, CScriptToken(LEX_ID, *it));
			}
			Tokens.insert(Tokens.begin()+tokenInsertIdx++, CScriptToken(';'));// insert at the begin var name; 
			Tokens[tokenBeginIdx].Int() = tokenInsertIdx-tokenBeginIdx;

			fix_BlockStarts_Marks(BlockStart, Marks, tokenBeginIdx, tokenInsertIdx-tokenBeginIdx);
		}
	}
}

void CScriptTokenizer::tokenizeVar(TOKEN_VECT &Tokens, bool &Statement, vector<int> &BlockStart, vector<int> &Marks) {
	if(!Statement)	throwTokenNotExpected();

	bool forward = TOKEN_VECT::size_type(BlockStart.front()) != Tokens.size(); // forwarde only if the var-Statement not the first Statement of the Scope

	Marks.push_back(pushToken(Tokens)); // push Token & push BeginIdx

	Statement = false;
	vector<string> vars;
	for(;;) 
	{
		vars.push_back(l->tkStr);
		pushToken(Tokens, LEX_ID);
		if(l->tk=='=') {
			pushToken(Tokens);
			while(l->tk != ';' && l->tk != ',' && l->tk != LEX_EOF)
				tokenizeToken(Tokens, Statement, BlockStart, Marks);
		}
		if(l->tk==',')
			pushToken(Tokens);
		else
			break;
	}
	pushToken(Tokens, ';');
	Statement = true;

	int tokenBeginIdx = Marks.back();
	Marks.pop_back(); // clean-up Marks
	Tokens[tokenBeginIdx].Int() = Tokens.size()-tokenBeginIdx;

	if(forward) // copy a var-deklaration at the begin of the scope
	{
		int tokenInsertIdx = tokenBeginIdx = BlockStart.front();

		Tokens.insert(Tokens.begin()+tokenInsertIdx++, CScriptToken(LEX_R_VAR));
		Tokens.insert(Tokens.begin()+tokenInsertIdx++, CScriptToken(LEX_ID, vars.front()));
		for(vector<string>::iterator it = vars.begin()+1; it != vars.end(); ++it) {
			Tokens.insert(Tokens.begin()+tokenInsertIdx++, CScriptToken(','));
			Tokens.insert(Tokens.begin()+tokenInsertIdx++, CScriptToken(LEX_ID, *it));
		}
		Tokens.insert(Tokens.begin()+tokenInsertIdx++, CScriptToken(';'));// insert at the begin var name; 
		Tokens[tokenBeginIdx].Int() = tokenInsertIdx-tokenBeginIdx;

		fix_BlockStarts_Marks(BlockStart, Marks, tokenBeginIdx, tokenInsertIdx-tokenBeginIdx);
	}
}


void CScriptTokenizer::tokenizeBlock(TOKEN_VECT &Tokens, bool &Statement, vector<int> &BlockStart, vector<int> &Marks) {
	int tkEnd = 0;
	bool Statement_at_end = false;
	switch(l->tk)
	{
	case'{': tkEnd = '}'; break;
	case'(': tkEnd = ')'; break;
	case'[': tkEnd = ']'; break;
	}
	Marks.push_back(pushToken(Tokens)); // push Token & push BeginIdx

	if(tkEnd == '}') // { ... } Block
	{
		if( (Statement_at_end = Statement) )		// Statment-Level after '}' is the same Level as at start ('{')
			BlockStart.push_back(Tokens.size());	// push Block-Start (one Token after '{')
	} else
		Statement = false;
	while(l->tk != tkEnd && l->tk != LEX_EOF) tokenizeToken(Tokens, Statement, BlockStart, Marks);
	pushToken(Tokens, tkEnd);

	if(tkEnd == '}' && Statement_at_end) { // { ... } Block
		BlockStart.pop_back(); // clean-up BlockStarts
	}

	int tokenBeginIdx = Marks.back();
	Marks.pop_back(); // clean-up Marks
	Tokens[tokenBeginIdx].Int() = Tokens.size()-tokenBeginIdx;

	Statement = Statement_at_end;
}
void CScriptTokenizer::tokenizeStatement(TOKEN_VECT &Tokens, bool &Statement, vector<int> &BlockStart, vector<int> &Marks) {
	do {
		tokenizeToken(Tokens, Statement, BlockStart, Marks); 
	} while(Statement == false && l->tk != LEX_EOF ); // tokenize one Statement
	Statement = true;
}
void CScriptTokenizer::tokenizeToken(TOKEN_VECT &Tokens, bool &Statement, vector<int> &BlockStart, vector<int> &Marks) {
	switch(l->tk)
	{
	case '(':
	case '[':
	case '{':				tokenizeBlock(Tokens, Statement, BlockStart, Marks); break;
	case LEX_R_VAR:		tokenizeVar(Tokens, Statement, BlockStart, Marks); break;
	case LEX_R_LET:		tokenizeLet(Tokens, Statement, BlockStart, Marks); break;
	case LEX_R_FUNCTION:	tokenizeFunction(Tokens, Statement, BlockStart, Marks); break;
	case LEX_R_FOR:		tokenizeFor(Tokens, Statement, BlockStart, Marks); break;
	case LEX_R_IF:			tokenizeIf(Tokens, Statement, BlockStart, Marks); break;
	case LEX_R_DO:			tokenizeDo(Tokens, Statement, BlockStart, Marks); break;
	case LEX_R_WHILE:		tokenizeWhile(Tokens, Statement, BlockStart, Marks); break;
	case LEX_R_WITH:		tokenizeWith(Tokens, Statement, BlockStart, Marks); break;
	case LEX_R_SWITCH:	tokenizeSwitch(Tokens, Statement, BlockStart, Marks); break;
	case LEX_R_TRY:		tokenizeTry(Tokens, Statement, BlockStart, Marks); break;
	case LEX_ID: {
			string str = l->tkStr;
			if(!Statement && (str=="get"||str=="set")) {
				tokenizeFunction(Tokens, Statement, BlockStart, Marks);
				break;
			}
			pushToken(Tokens);
			if(Statement && l->tk==':') { // label
				Tokens[Tokens.size()-1].token = LEX_T_LABEL; // change LEX_ID to LEX_T_LABEL
				pushToken(Tokens);
				break;
			}
		}
		Statement = false;
		break;
	default:
		Statement = l->tk==';'; // after ';' allways Statement-Level
		pushToken(Tokens);
	}
#if 0	
	if(l->tk == '{' || l->tk == '(' || l->tk == '[') {	// block
		tokenizeBlock(Tokens, Statement, BlockStart, Marks);
	} else if(l->tk == LEX_R_VAR) {
		tokenizeVar(Tokens, Statement, BlockStart, Marks);
	} else if(l->tk == LEX_R_FUNCTION) {
		tokenizeFunction(Tokens, Statement, BlockStart, Marks);
	} else if(l->tk == LEX_R_FOR) {
		tokenizeFor(Tokens, Statement, BlockStart, Marks);
	} else if(l->tk == LEX_R_IF) {
		tokenizeIf(Tokens, Statement, BlockStart, Marks);
	} else if(l->tk == LEX_R_DO) {
		tokenizeDo(Tokens, Statement, BlockStart, Marks);
	} else if(l->tk == LEX_R_WHILE) {
		tokenizeWhile(Tokens, Statement, BlockStart, Marks);
	} else if(l->tk == LEX_R_WITH) {
		tokenizeWith(Tokens, Statement, BlockStart, Marks);
	} else if(l->tk == LEX_R_TRY) {
		tokenizeTry(Tokens, Statement, BlockStart, Marks);
	} else if(l->tk == LEX_ID && (l->tkStr=="get"||l->tkStr=="set")) {
		string::const_iterator prev_pos=l->tokenStart;
		string::const_iterator prev_line_start=l->currentLineStart;
		int prev_line = l->currentLine;
		string tkStr = l->tkStr;
		l->match(l->tk);
		bool is_set_get = l->tk == LEX_ID; // true if set/get LEX_ID
		l->reset(prev_pos, prev_line, prev_line_start);
		if(is_set_get) {
			l->tk = tkStr=="get"?LEX_T_GET:LEX_T_SET;
			tokenizeFunction(Tokens, Statement, BlockStart, Marks);
		}
		else
			pushToken(Tokens);
	} else {
		Statement = l->tk==';'; // after ';' allways Statement-Level
		pushToken(Tokens);
	}
#endif
}
int CScriptTokenizer::pushToken(TOKEN_VECT &Tokens, int Match) {
	Tokens.push_back(CScriptToken(l, Match));
	return Tokens.size()-1;
}

void CScriptTokenizer::throwTokenNotExpected() {
	throw new CScriptException("'"+CScriptLex::getTokenStr(l->tk)+"' was not expected", l->currentFile, l->currentLine, l->currentColumn());
}

// ----------------------------------------------------------------------------------- CSCRIPTVARLINK

CScriptVarLink::CScriptVarLink(const CScriptVarPtr &Var, const string &Name /*=TINYJS_TEMP_NAME*/, int Flags /*=SCRIPTVARLINK_DEFAULT*/) 
	: name(Name), owner(0), flags(Flags&~SCRIPTVARLINK_OWNED) {
#if DEBUG_MEMORY
	mark_allocated(this);
#endif
	var = Var;
}

CScriptVarLink::CScriptVarLink(const CScriptVarLink &link) 
	: name(link.name), owner(0), flags(SCRIPTVARLINK_DEFAULT) {
	// Copy constructor
#if DEBUG_MEMORY
	mark_allocated(this);
#endif
	var = link.var;
}

CScriptVarLink::~CScriptVarLink() {
#if DEBUG_MEMORY
	mark_deallocated(this);
#endif
}

void CScriptVarLink::replaceWith(const CScriptVarPtr &newVar) {
	if(!newVar) ASSERT(0);//newVar = new CScriptVar();
	var = newVar;
}

// ----------------------------------------------------------------------------------- CSCRIPTVAR

CScriptVar::CScriptVar(CTinyJS *Context, const CScriptVarPtr &Prototype) {
	context = Context;
	temporaryID = 0;
	if(context->first) {
		next = context->first;
		next->prev = this;
	} else {
		next = 0;
	}
	context->first = this;
	prev = 0;
	refs = 0;
	if(Prototype)
		addChild(TINYJS___PROTO___VAR, Prototype, SCRIPTVARLINK_WRITABLE);
#if DEBUG_MEMORY
	mark_allocated(this);
#endif
}
CScriptVar::CScriptVar(const CScriptVar &Copy) {
	context = Copy.context;
	temporaryID = 0;
	if(context->first) {
		next = context->first;
		next->prev = this;
	} else {
		next = 0;
	}
	context->first = this;
	prev = 0;
	refs = 0;
	SCRIPTVAR_CHILDS_cit it;
	for(it = Copy.Childs.begin(); it!= Copy.Childs.end(); ++it) {
		addChild((*it)->getName(), (*it), (*it)->getFlags());
	}

#if DEBUG_MEMORY
	mark_allocated(this);
#endif
}
CScriptVar::~CScriptVar(void) {
#if DEBUG_MEMORY
	mark_deallocated(this);
#endif
	removeAllChildren();
	if(prev)
		prev->next = next;
	else
		context->first = next;
	if(next)
		next->prev = prev;
}

/// Type

bool CScriptVar::isObject()		{return false;}
bool CScriptVar::isArray()			{return false;}
bool CScriptVar::isAccessor()		{return false;}
bool CScriptVar::isNull()			{return false;}
bool CScriptVar::isUndefined()	{return false;}
bool CScriptVar::isNaN()			{return false;}
bool CScriptVar::isString()		{return false;}
bool CScriptVar::isInt()			{return false;}
bool CScriptVar::isBool()			{return false;}
int CScriptVar::isInfinity()		{ return 0; } ///< +1==POSITIVE_INFINITY, -1==NEGATIVE_INFINITY, 0==is not an InfinityVar
bool CScriptVar::isDouble()		{return false;}
bool CScriptVar::isNumber()		{return false;}
bool CScriptVar::isNumeric()		{return false;}
bool CScriptVar::isPrimitive()	{return true;}
bool CScriptVar::isFunction()		{return false;}
bool CScriptVar::isNative()		{return false;}


/// Value
int CScriptVar::getInt() {return 0;}
bool CScriptVar::getBool() {return false;}
double CScriptVar::getDouble() {return 0.0;}
CScriptTokenDataFnc *CScriptVar::getFunctionData() { return 0; }
string CScriptVar::getString() {return "";}

string CScriptVar::getParsableString(const string &indentString, const string &indent) {
	return getString();
}

CScriptVarPtr CScriptVar::getNumericVar() {
	return constScriptVar(NaN);
}
CScriptVarPtr CScriptVar::getPrimitivVar() {
	bool execute=true;
	CScriptVarPtr var = getPrimitivVar(execute);
//	if(!execute) TODO
	return var;
}
CScriptVarPtr CScriptVar::getPrimitivVar(bool execute) {
	if(execute) {
		if(!isPrimitive()) {
			CScriptVarPtr ret = valueOf(execute);
			if(execute && !ret->isPrimitive()) {
				ret = toString(execute);
				if(execute && !ret->isPrimitive())
					do{}while(0); // TODO error can't convert in primitive type
			}
			return ret;
		}
		return this;
	}
	return constScriptVar(Undefined);
}

CScriptVarPtr CScriptVar::valueOf(bool execute) {
	if(execute) {
		CScriptVarPtr FncValueOf = findChildWithPrototypeChain("valueOf");
		if(FncValueOf != context->objectPrototype_valueOf) { // custom valueOf in JavaScript
			vector<CScriptVarPtr> Params;
			return context->callFunction(execute, FncValueOf, Params, this);
		} else {
			return _valueOf(execute);
		}
	}
	return this;
}
CScriptVarPtr CScriptVar::_valueOf(bool execute) {
	return this;
}

CScriptVarPtr CScriptVar::toString(bool execute, int radix/*=0*/) {
	if(execute) {
		CScriptVarPtr FncToString = findChildWithPrototypeChain("toString");
		if(FncToString != context->objectPrototype_toString) { // custom valueOf in JavaScript
			vector<CScriptVarPtr> Params;
			Params.push_back(newScriptVar(radix));
			return context->callFunction(execute, FncToString, Params, this);
		} else {
			return _toString(execute, radix);
		}
	}
	return this;
}
CScriptVarPtr CScriptVar::_toString(bool execute, int radix/*=0*/) {
	return this;
}

////// Childs

/// find
static bool compare_child_name(CScriptVarLink *Link, const string &Name) {
	return Link->getName() < Name;
}

CScriptVarLink *CScriptVar::findChild(const string &childName) {
	if(Childs.empty()) return 0;
	SCRIPTVAR_CHILDS_it it = lower_bound(Childs.begin(), Childs.end(),
		childName.c_str(),
		compare_child_name);
	if(it != Childs.end() && (*it)->getName() == childName)
		return *it;
	return 0;
}

CScriptVarLink *CScriptVar::findChildInPrototypeChain(const string &childName) {
	unsigned int uniqueID = context->getUniqueID();
	// Look for links to actual parent classes
	CScriptVarPtr object = this;
	CScriptVarLink *__proto__;
	while( object->getTempraryID() != uniqueID && (__proto__ = object->findChild(TINYJS___PROTO___VAR)) ) {
		CScriptVarLink *implementation = (*__proto__)->findChild(childName);
		if (implementation) return implementation;
		object->setTemporaryID(uniqueID); // prevents recursions
		object = __proto__;
	}
	return 0;
}

CScriptVarLink *CScriptVar::findChildWithPrototypeChain(const string &childName) {
	unsigned int uniqueID = context->getUniqueID();
	CScriptVarPtr object = this;
	while( object && object->getTempraryID() != uniqueID) {
		CScriptVarLink *implementation = object->findChild(childName);
		if (implementation) return implementation;
		object->setTemporaryID(uniqueID); // prevents recursions
		object = object->findChild(TINYJS___PROTO___VAR);
	}
	return 0;
}
CScriptVarLink *CScriptVar::findChildByPath(const string &path) {
	string::size_type p = path.find('.');
	CScriptVarLink *child;
	if (p == string::npos)
		return findChild(path);
	if( (child = findChild(path.substr(0,p))) )
		(*child)->findChildByPath(path.substr(p+1));
	return 0;
}

CScriptVarLink *CScriptVar::findChildOrCreate(const string &childName/*, int varFlags*/) {
	CScriptVarLink *l = findChild(childName);
	if (l) return l;
	return addChild(childName, constScriptVar(Undefined));
	//	return addChild(childName, new CScriptVar(context, TINYJS_BLANK_DATA, varFlags));
}

CScriptVarLink *CScriptVar::findChildOrCreateByPath(const string &path) {
	string::size_type p = path.find('.');
	if (p == string::npos)
		return findChildOrCreate(path);
	string childName(path, 0, p);
	CScriptVarLink *l = findChild(childName);
	if (!l) l = addChild(childName, newScriptVar(Object));
	return (*l)->findChildOrCreateByPath(path.substr(p+1));
}

/// add & remove
CScriptVarLink *CScriptVar::addChild(const string &childName, const CScriptVarPtr &child, int linkFlags /*= SCRIPTVARLINK_DEFAULT*/) {
CScriptVarLink *link = 0;
	SCRIPTVAR_CHILDS_it it = lower_bound(Childs.begin(), Childs.end(), childName, compare_child_name);
	if(it == Childs.end() || (*it)->getName() != childName) {
		link = new CScriptVarLink(child?child:constScriptVar(Undefined), childName, linkFlags);
		link->setOwner(this);
		link->setOwned(true);
		Childs.insert(it, link);
#ifdef _DEBUG
	} else {
		ASSERT(0); // addChild - the child exists 
#endif
	}
	return link;
}
CScriptVarLink *CScriptVar::addChildNoDup(const string &childName, const CScriptVarPtr &child, int linkFlags /*= SCRIPTVARLINK_DEFAULT*/) {
	SCRIPTVAR_CHILDS_it it = lower_bound(Childs.begin(), Childs.end(), childName, compare_child_name);
	if(it == Childs.end() || (*it)->getName() != childName) {
		CScriptVarLink *link = new CScriptVarLink(child, childName, linkFlags);
		link->setOwner(this);
		link->setOwned(true);
		Childs.insert(it, link);
		return link;
	} else {
		(*it)->replaceWith(child);
		return (*it);
	}
}

bool CScriptVar::removeLink(CScriptVarLinkPtr &Link) {
	CScriptVarLink *link = Link.getRealLink();
	Link.clear();
	return removeLink(link);
}

bool CScriptVar::removeLink(CScriptVarLink *&link) {
	if (!link) return false;
	SCRIPTVAR_CHILDS_it it = lower_bound(Childs.begin(), Childs.end(), link->getName(), compare_child_name);
	if(it != Childs.end() && (*it) == link) {
		Childs.erase(it);
#ifdef _DEBUG
	} else {
		ASSERT(0); // removeLink - the link is not atached to this var 
#endif
	}
	delete link;
	link = 0;
	return true;
}
void CScriptVar::removeAllChildren() {
	SCRIPTVAR_CHILDS_it it;
	for(it = Childs.begin(); it!= Childs.end(); ++it) {
		delete *it;
	}
	Childs.clear();
}

CScriptVarPtr CScriptVar::getArrayIndex(int idx) {
	CScriptVarLink *link = findChild(int2string(idx));
	if (link) return link;
	else return constScriptVar(Undefined); // undefined
}

void CScriptVar::setArrayIndex(int idx, const CScriptVarPtr &value) {
	string sIdx = int2string(idx);
	CScriptVarLink *link = findChild(sIdx);

	if (link) {
		if (value->isUndefined())
			removeLink(link);
		else
			link->replaceWith(value);
	} else {
		if (!value->isUndefined())
			addChild(sIdx, value);
	}
}

int CScriptVar::getArrayLength() {
	int highest = -1;
	if (!isArray()) return 0;

	for(SCRIPTVAR_CHILDS_it it = Childs.begin(); it != Childs.end(); ++it) {
		if (::isNumber((*it)->getName())) {
			int val = atoi((*it)->getName().c_str());
			if (val > highest) highest = val;
		}
	}
	return highest+1;
}

template <typename T>
CScriptVarPtr DoMaths(CScriptVarPtr &a, CScriptVarPtr &b, int op)
{
	int dai = a->getInt();// use int when needed
	int dbi = b->getInt();
	T da = (T)(a->isDouble()?a->getDouble():dai);
	T db = (T)(b->isDouble()?b->getDouble():dbi);
	switch (op) {
			case '+':			return a->newScriptVar(da+db);
			case '-':			return a->newScriptVar(da-db);
			case '*':			return a->newScriptVar(da*db);
			case '/':
				if(db==0) {
					if(da==0) 	return a->constScriptVar(NaN);			// 0/0 = NaN
					else 			return a->constScriptVar(Infinity(da<0 ? -1 : 1));	//  /0 = Infinity
				} else			return a->newScriptVar(da/db);
			case '%':
				if(db==0) 		return a->constScriptVar(NaN);			// 0/0 = NaN
				else				return a->newScriptVar(dai%dbi);
			case '&':			return a->newScriptVar(dai&dbi);
			case '|':			return a->newScriptVar(dai|dbi);
			case '^':			return a->newScriptVar(dai^dbi);
			case '~':			return a->newScriptVar(~dai);
			case LEX_LSHIFT:	return a->newScriptVar(dai<<dbi);
			case LEX_RSHIFT:	return a->newScriptVar(dai>>dbi);
			case LEX_RSHIFTU:	return a->newScriptVar((int)(((unsigned int)dai)>>dbi));
			case LEX_EQUAL:	return a->constScriptVar(da==db);
			case LEX_NEQUAL:	return a->constScriptVar(da!=db);
			case '<':			return a->constScriptVar(da<db);
			case LEX_LEQUAL:	return a->constScriptVar(da<=db);
			case '>':			return a->constScriptVar(da>db);
			case LEX_GEQUAL:	return a->constScriptVar(da>=db);
			default: throw new CScriptException("This operation not supported on the int datatype");
	}
}
#define RETURN_NAN return constScriptVar(NaN)
CScriptVarPtr CScriptVar::mathsOp(const CScriptVarPtr &b, int op) {
	bool execute = true;
	return context->mathsOp(execute, this, b, op);
#if 0
	CScriptVarPtr a = this;
	// TODO Equality checks on classes/structures
	// Type equality check
	if (op == LEX_TYPEEQUAL || op == LEX_NTYPEEQUAL) {
		// check type first, then call again to check data
		bool eql = false;
		if(!a->isNaN() && !b->isNaN() && typeid(*a.getVar()) == typeid(*b.getVar()) ) {
			CScriptVarPtr e = a->mathsOp(b, LEX_EQUAL);
			eql = e->getBool();
		}
		if (op == LEX_TYPEEQUAL)
			return constScriptVar(eql);
		else
			return constScriptVar(!eql);
	}
	// do maths...
	bool a_isString = a->isString();
	bool b_isString = b->isString();
	// special for strings and string '+'
	// both a String or one a String and op='+'
	if( (a_isString && b_isString) || ((a_isString || b_isString) && op == '+')) {
		string da = a->getString();
		string db = b->getString();
		switch (op) {
		case '+':			return newScriptVar(da+db);
		case LEX_EQUAL:	return constScriptVar(da==db);
		case LEX_NEQUAL:	return constScriptVar(da!=db);
		case '<':			return constScriptVar(da<db);
		case LEX_LEQUAL:	return constScriptVar(da<=db);
		case '>':			return constScriptVar(da>db);
		case LEX_GEQUAL:	return constScriptVar(da>=db);
		default:				RETURN_NAN;
		}
	}
	// special for undefined and null
	else if( (a->isUndefined() || a->isNull()) && (b->isUndefined() || b->isNull()) ) {
		switch (op) {
		case LEX_NEQUAL:	return constScriptVar( !( ( a->isUndefined() || a->isNull() ) && ( b->isUndefined() || b->isNull() ) ) );
		case LEX_EQUAL:	return constScriptVar(    ( a->isUndefined() || a->isNull() ) && ( b->isUndefined() || b->isNull() )   );
		case LEX_GEQUAL:	
		case LEX_LEQUAL:
		case '<':
		case '>':			return constScriptVar(false);
		default:				RETURN_NAN;
		}
	}
	if (a->isArray() && b->isArray()) {
		/* Just check pointers */
		switch (op) {
		case LEX_EQUAL:	return constScriptVar(a==b);
		case LEX_NEQUAL:	return constScriptVar(a!=b);
		}
	} else if (a->isObject() && b->isObject()) {
		/* Just check pointers */
		switch (op) {
		case LEX_EQUAL:	return constScriptVar(a==b);
		case LEX_NEQUAL:	return constScriptVar(a!=b);
		}
	}
	
	// gets only an Integer, a Double, in Infinity or a NaN
	CScriptVarPtr a_l = a->getNumericVar();
	CScriptVarPtr b_l = b->getNumericVar();
	{
		CScriptVarPtr a = a_l;
		CScriptVarPtr b = b_l;

		if( a->isNaN() || b->isNaN() ) {
			switch (op) {
			case LEX_NEQUAL:	return constScriptVar(true);
			case LEX_EQUAL:
			case LEX_GEQUAL:	
			case LEX_LEQUAL:
			case '<':
			case '>':			return constScriptVar(false);
			default:				RETURN_NAN;
			}
		}
		else if((a->isInfinity() || b->isInfinity())) {
			int tmp = 0;
			int a_i=a->isInfinity(), a_sig=a->getInt()>0?1:-1;
			int b_i=b->isInfinity(), b_sig=a->getInt()>0?1:-1;
			switch (op) {
			case LEX_EQUAL:	return constScriptVar(a_i == b_i);
			case LEX_GEQUAL:	
			case '>':			return constScriptVar(a_i >= b_i);
			case LEX_LEQUAL:	
			case '<':			return constScriptVar(a_i <= b_i);
			case LEX_NEQUAL:	return constScriptVar(a_i != b_i);
			case '+':			if(a_i && b_i && a_i != b_i) RETURN_NAN;
									return constScriptVar(Infinity(b_i?b_i:a_i));
			case '-':			if(a_i && a_i == b_i) RETURN_NAN;
									return constScriptVar(Infinity(b_i?-b_i:a_i));
			case '*':			tmp = a->getInt() * b->getInt();
				if(tmp == 0)	RETURN_NAN;
									return constScriptVar(Infinity(tmp));
			case '/':			if(a_i && b_i) RETURN_NAN;
				if(b_i)			return newScriptVar(0);
									return constScriptVar(Infinity(a_sig*b_sig));
			case '%':			if(a_i) RETURN_NAN;
									return constScriptVar(Infinity(a_sig));
			case '&':			return newScriptVar( 0);
			case '|':
			case '^':			if(a_i && b_i) return newScriptVar( 0);
									return newScriptVar(a_i?b->getInt():a->getInt());
			case LEX_LSHIFT:
			case LEX_RSHIFT:	
			case LEX_RSHIFTU:	if(a_i) return newScriptVar(0);
									return newScriptVar(a->getInt());
			default:				throw new CScriptException("This operation not supported on the int datatype");
			}
		} else {
			if (!a->isDouble() && !b->isDouble()) {
				// use ints
				return DoMaths<int>(a, b, op);
			} else {
				// use doubles
				return DoMaths<double>(a, b, op);
			}
		}
	}
	
	ASSERT(0);
	return CScriptVarPtr();
#endif
}

void CScriptVar::trace(const string &name) {
	string indentStr;
	uint32_t uniqueID = context->getUniqueID();
	trace(indentStr, uniqueID, name);
}
void CScriptVar::trace(string &indentStr, uint32_t uniqueID, const string &name) {
	string indent = "  ";
	const char *extra="";
	if(temporaryID == uniqueID)
		extra = " recursion detected";
	TRACE("%s'%s' = '%s' %s%s\n",
			indentStr.c_str(),
			name.c_str(),
			getString().c_str(),
			getFlagsAsString().c_str(),
			extra);
	if(temporaryID != uniqueID) {
		temporaryID = uniqueID;
		indentStr+=indent;
		for(SCRIPTVAR_CHILDS_it it = Childs.begin(); it != Childs.end(); ++it) {
			if((*it)->isEnumerable())
				(*(*it))->trace(indentStr, uniqueID, (*it)->getName());
		}
		indentStr = indentStr.substr(0, indentStr.length()-2);
	}
}

string CScriptVar::getFlagsAsString() {
	string flagstr = "";
	if (isFunction()) flagstr = flagstr + "FUNCTION ";
	if (isObject()) flagstr = flagstr + "OBJECT ";
	if (isArray()) flagstr = flagstr + "ARRAY ";
	if (isNative()) flagstr = flagstr + "NATIVE ";
	if (isDouble()) flagstr = flagstr + "DOUBLE ";
	if (isInt()) flagstr = flagstr + "INTEGER ";
	if (isBool()) flagstr = flagstr + "BOOLEAN ";
	if (isString()) flagstr = flagstr + "STRING ";
	if (isNaN()) flagstr = flagstr + "NaN ";
	if (isInfinity()) flagstr = flagstr + "INFINITY ";
	return flagstr;
}

#ifdef TODO
void CScriptVar::getJSON(ostringstream &destination, const string linePrefix) {
	if (isObject()) {
		string indentedLinePrefix = linePrefix+"  ";
		// children - handle with bracketed list
		destination << "{ \n";
		int count = Childs.size();
		for(SCRIPTVAR_CHILDS_it it = Childs.begin(); it != Childs.end(); ++it) {
			destination << indentedLinePrefix;
			if (isAlphaNum((*it)->getName()))
				destination  << (*it)->getName();
			else
				destination  << getJSString((*it)->getName());
			destination  << " : ";
			(*(*it))->getJSON(destination, indentedLinePrefix);
			if (--count) destination  << ",\n";
		}
		destination << "\n" << linePrefix << "}";
	} else if (isArray()) {
		string indentedLinePrefix = linePrefix+"  ";
		destination << "[\n";
		int len = getArrayLength();
		if (len>10000) len=10000; // we don't want to get stuck here!

		for (int i=0;i<len;i++) {
			getArrayIndex(i)->getJSON(destination, indentedLinePrefix);
			if (i<len-1) destination  << ",\n";
		}

		destination << "\n" << linePrefix << "]";
	} else {
		// no children or a function... just write value directly
		destination << getParsableString();
	}
}
#endif


CScriptVar *CScriptVar::ref() {
	refs++;
	return this;
}
void CScriptVar::unref() {
	refs--;
	ASSERT(refs>=0); // printf("OMFG, we have unreffed too far!\n");
	if (refs==0)
		delete this;
}

int CScriptVar::getRefs() {
	return refs;
}

void CScriptVar::setTemporaryID_recursive(uint32_t ID) {
	if(temporaryID != ID) {
		temporaryID = ID;
		for(SCRIPTVAR_CHILDS_it it = Childs.begin(); it != Childs.end(); ++it) {
			(*(*it))->setTemporaryID_recursive(ID);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
/// CScriptVarLinkPtr
//////////////////////////////////////////////////////////////////////////

CScriptVarLinkPtr &CScriptVarLinkPtr::operator()(CScriptVarLink *Link) {
	bool execute = true;
	(*this)(execute, Link);
	return *this;
}
CScriptVarLinkPtr &CScriptVarLinkPtr::operator()(bool &execute, CScriptVarLink *Link) {
	ASSERT(!Link || Link->owner); // no add not Owned Links
	link = Link;
	if(link && (*link)->isAccessor()) {
		CScriptVarLink *getter = (*link)->findChild(TINYJS_ACCESSOR_GET_VAR);
		if(getter) {
			std::vector<CScriptVarPtr> Params;
			ASSERT(link->owner);
			set_tmp_link((*getter)->getContext()->callFunction(execute, getter, Params, link->owner));
		} else
			set_tmp_link((*link)->constScriptVar(Undefined));
	}else
		clear_tmp_link();
	return *this;
}

void CScriptVarLinkPtr::replaceVar(bool &execute, const CScriptVarPtr &Var) {
	if(link && (*link)->isAccessor()) {
		CScriptVarLink *setter = (*link)->findChild(TINYJS_ACCESSOR_SET_VAR);
		if(setter) {
			std::vector<CScriptVarPtr> Params;
			Params.push_back(Var);
			bool execute;
			ASSERT(link->owner);
			(*setter)->getContext()->callFunction(execute, setter, Params, link->owner);
		}
	} else
		getLink()->var = Var;
}

void CScriptVarLinkPtr::swap(CScriptVarLinkPtr &Link){ 
	CScriptVarLink *_link = link;	link = Link.link; Link.link = _link;
	CScriptVarLinkTmpPtr _tmp_link = tmp_link; 	tmp_link = Link.tmp_link; Link.tmp_link = _tmp_link;
}

void CScriptVarLinkPtr::set_tmp_link(const CScriptVarPtr &Var, const std::string &Name /*= TINYJS_TEMP_NAME*/, int Flags /*= SCRIPTVARLINK_DEFAULT*/) {
	// refs()==1 makes more like a pointer
	// if refs==1 then only this LinkPtr owns the tmp_link
	// otherwise we creates a new one
	if(tmp_link.refs()==1) {
		tmp_link->name = Name;
		tmp_link->owner = 0;
		tmp_link->flags = Flags&~SCRIPTVARLINK_OWNED;
		tmp_link->var = Var;
	} else if(tmp_link.refs()==0)
		tmp_link = CScriptVarLinkTmpPtr(Var, Name, Flags);
	else
		tmp_link = CScriptVarLinkTmpPtr(Var, Name, Flags);
}

void CScriptVarLinkPtr::clear_tmp_link(){
	if(tmp_link.refs()==1)
		tmp_link->var.clear();
	else if(tmp_link.refs()>1)
		tmp_link = CScriptVarLinkTmpPtr();
}

////////////////////////////////////////////////////////////////////////// CScriptVarObject

declare_dummy_t(Object);
CScriptVarObject::CScriptVarObject(CTinyJS *Context) : CScriptVar(Context, Context->objectPrototype) { }
CScriptVarObject::~CScriptVarObject() {}
CScriptVarPtr CScriptVarObject::clone() { return new CScriptVarObject(*this); }
bool CScriptVarObject::isObject() { return true; }
bool CScriptVarObject::isPrimitive() { return false; } 

int CScriptVarObject::getInt() { return getPrimitivVar()->getInt(); }
bool CScriptVarObject::getBool() { return getPrimitivVar()->getBool(); }
double CScriptVarObject::getDouble() { return getPrimitivVar()->getDouble(); }
std::string CScriptVarObject::getString() { return getPrimitivVar()->getString(); }

string CScriptVarObject::getParsableString(const string &indentString, const string &indent) {
	ostringstream destination;
	string nl = indent.size() ? "\n" : " ";

	destination << "{";
	if(Childs.size()) {
		string new_indentString = indentString + indent;
		int count = 0;
		destination << nl;
		for(SCRIPTVAR_CHILDS_it it = Childs.begin(); it != Childs.end(); ++it) {
			if((*it)->isEnumerable()) {
				if (count++) destination  << "," << nl;
				if (isAlphaNum((*it)->getName()))
					destination << new_indentString << (*it)->getName();
				else
					destination << new_indentString << "\"" << getJSString((*it)->getName()) << "\"";
				destination  << " : ";
				destination << (*(*it))->getParsableString(new_indentString, indent);
			}
		}
		destination << nl << indentString;
	}
	destination << "}";
	return destination.str();
}
string CScriptVarObject::getVarType() { return "object"; }
CScriptVarPtr CScriptVarObject::_toString(bool execute, int radix) { return newScriptVar("[ Object ]"); };
////////////////////////////////////////////////////////////////////////// CScriptVarObject

declare_dummy_t(ObjectWrap);

CScriptVarObjectWrap::CScriptVarObjectWrap(CTinyJS *Context, const CScriptVarPtr &Value) : CScriptVarObject(Context), value(Value) {
	ASSERT(value);
	CScriptVarLink *proto = Value->findChild(TINYJS___PROTO___VAR);
	this->addChildNoDup(TINYJS___PROTO___VAR, proto, SCRIPTVARLINK_WRITABLE);
}
CScriptVarObjectWrap::~CScriptVarObjectWrap(){}
CScriptVarPtr CScriptVarObjectWrap::clone() { return new CScriptVarObjectWrap(*this); }
CScriptVarPtr CScriptVarObjectWrap::_valueOf(bool execute) { return value; }
CScriptVarPtr CScriptVarObjectWrap::_toString(bool execute, int radix/*=0*/) { return value; }
std::string CScriptVarObjectWrap::getParsableString( const std::string &indentString, const std::string &indent ) {
	return value->getParsableString(indentString, indent);
}
void CScriptVarObjectWrap::setTemporaryID_recursive(uint32_t ID) {
	CScriptVar::setTemporaryID_recursive(ID);
	value->setTemporaryID_recursive(ID);
}

////////////////////////////////////////////////////////////////////////// CScriptVarAccessor

declare_dummy_t(Accessor);
CScriptVarAccessor::CScriptVarAccessor(CTinyJS *Context) : CScriptVar(Context, Context->objectPrototype) { }
CScriptVarAccessor::~CScriptVarAccessor() {}
CScriptVarPtr CScriptVarAccessor::clone() { return new CScriptVarAccessor(*this); }
bool CScriptVarAccessor::isAccessor() { return true; }
bool CScriptVarAccessor::isPrimitive()	{ return false; } 
string CScriptVarAccessor::getString() { return "[ Object ]"; };
string CScriptVarAccessor::getParsableString(const string &indentString, const string &indent) {

	/*
	ostringstream destination;
	string nl = indent.size() ? "\n" : " ";

	destination << "{";
	int count = Childs.size();
	if(count) {
		string new_indentString = indentString + indent;

		destination << nl;
		for(SCRIPTVAR_CHILDS_it it = Childs.begin(); it != Childs.end(); ++it) {
			if (isAlphaNum((*it)->getName()))
				destination << new_indentString << (*it)->getName();
			else
				destination << new_indentString << "\"" << getJSString((*it)->getName()) << "\"";
			destination  << " : ";
			destination << (*(*it))->getParsableString(new_indentString, indent);
			if (--count) destination  << "," << nl;
		}
		destination << nl << indentString;
	}
	destination << "}";
	return destination.str();
*/
	return "";
}
string CScriptVarAccessor::getVarType() { return "accessor"; }



////////////////////////////////////////////////////////////////////////// CScriptVarArray

declare_dummy_t(Array);
CScriptVarArray::CScriptVarArray(CTinyJS *Context) : CScriptVar(Context, Context->arrayPrototype) {
	CScriptVarLink *acc = addChild("length", newScriptVar(Accessor), 0);
	CScriptVarFunctionPtr getter(::newScriptVar(Context, this, &CScriptVarArray::native_Length, 0));
	getter->setFunctionData(new CScriptTokenDataFnc);
	(*acc)->addChild(TINYJS_ACCESSOR_GET_VAR, getter, 0);
}

CScriptVarArray::~CScriptVarArray() {}
CScriptVarPtr CScriptVarArray::clone() { return new CScriptVarArray(*this); }
bool CScriptVarArray::isArray() { return true; }
bool CScriptVarArray::isPrimitive()	{ return false; } 
string CScriptVarArray::getString() { 
	ostringstream destination;
	int len = getArrayLength();
	for (int i=0;i<len;i++) {
		destination << getArrayIndex(i)->getString();
		if (i<len-1) destination  << ", ";
	}
	return destination.str();
}
string CScriptVarArray::getVarType() { return "object"; }
string CScriptVarArray::getParsableString(const string &indentString, const string &indent) {
	ostringstream destination;
	string nl = indent.size() ? "\n" : " ";

	destination << "[";
	int len = getArrayLength();
	if(len) {
		destination << nl;
		string new_indentString = indentString + indent;
		for (int i=0;i<len;i++) {
			destination << new_indentString << getArrayIndex(i)->getParsableString(new_indentString, indent);
			if (i<len-1) destination  << "," << nl;
		}
		destination << nl << indentString;
	}
	destination << "]";
	return destination.str();
}

void CScriptVarArray::native_Length(const CFunctionsScopePtr &c, void *data)
{
	c->setReturnVar(newScriptVar(c->getArgument("this")->getArrayLength()));
}

////////////////////////////////////////////////////////////////////////// CScriptVarNull

declare_dummy_t(Null);
CScriptVarNull::CScriptVarNull(CTinyJS *Context) : CScriptVar(Context, Context->objectPrototype) { }
CScriptVarNull::~CScriptVarNull() {}
CScriptVarPtr CScriptVarNull::clone() { return new CScriptVarNull(*this); }
bool CScriptVarNull::isNull() { return true; }
bool CScriptVarNull::isNumeric() {return true; }
string CScriptVarNull::getString() { return "null"; };
string CScriptVarNull::getVarType() { return "null"; }
CScriptVarPtr CScriptVarNull::getNumericVar() { return newScriptVar(0); }


////////////////////////////////////////////////////////////////////////// CScriptVarUndefined

declare_dummy_t(Undefined);
CScriptVarUndefined::CScriptVarUndefined(CTinyJS *Context) : CScriptVar(Context, Context->objectPrototype) { }
CScriptVarUndefined::~CScriptVarUndefined() {}
CScriptVarPtr CScriptVarUndefined::clone() { return new CScriptVarUndefined(*this); }
bool CScriptVarUndefined::isUndefined() { return true; }
bool CScriptVarUndefined::isNumeric() {return true; }
string CScriptVarUndefined::getString() { return "undefined"; };
string CScriptVarUndefined::getVarType() { return "undefined"; }


////////////////////////////////////////////////////////////////////////// CScriptVarNaN

declare_dummy_t(NaN);
CScriptVarNaN::CScriptVarNaN(CTinyJS *Context) : CScriptVar(Context, Context->numberPrototype) {}
CScriptVarNaN::~CScriptVarNaN() {}
CScriptVarPtr CScriptVarNaN::clone() { return new CScriptVarNaN(*this); }
bool CScriptVarNaN::isNaN() { return true; }
bool CScriptVarNaN::isNumeric() {return true; }
string CScriptVarNaN::getString() { return "NaN"; };
string CScriptVarNaN::getVarType() { return "number"; }


////////////////////////////////////////////////////////////////////////// CScriptVarString

CScriptVarString::CScriptVarString(CTinyJS *Context, const std::string &Data) : CScriptVar(Context, Context->stringPrototype), data(Data) {
	CScriptVarLink *acc = addChild("length", newScriptVar(Accessor), 0);
	CScriptVarFunctionPtr getter(::newScriptVar(Context, this, &CScriptVarString::native_Length, 0));
	getter->setFunctionData(new CScriptTokenDataFnc);
	(*acc)->addChild(TINYJS_ACCESSOR_GET_VAR, getter, 0);
}
CScriptVarString::~CScriptVarString() {}
CScriptVarPtr CScriptVarString::clone() { return new CScriptVarString(*this); }
bool CScriptVarString::isString() { return true; }
int CScriptVarString::getInt() {return strtol(data.c_str(),0,0); }
bool CScriptVarString::getBool() {return data.length()!=0;}
double CScriptVarString::getDouble() {return strtod(data.c_str(),0);}
string CScriptVarString::getString() { return data; }
string CScriptVarString::getParsableString() { return getJSString(data); }
string CScriptVarString::getVarType() { return "string"; }
CScriptVarPtr CScriptVarString::getNumericVar() {
	string the_string = getString();
	double d;
	char *endptr;//=NULL;
	int i = strtol(the_string.c_str(),&endptr,0);
	if(*endptr == '\0')
		return newScriptVar(i);
	if(*endptr=='.' || *endptr=='e' || *endptr=='E') {
		d = strtod(the_string.c_str(),&endptr);
		if(*endptr == '\0')
			return newScriptVar(d);
	}
	return CScriptVar::getNumericVar();
}

void CScriptVarString::native_Length(const CFunctionsScopePtr &c, void *data) {
	c->setReturnVar(newScriptVar((int)this->data.size()));
}


////////////////////////////////////////////////////////////////////////// CScriptVarIntegerBase

CScriptVarIntegerBase::CScriptVarIntegerBase(CTinyJS *Context, const CScriptVarPtr &Prototype, int Data) : CScriptVar(Context, Prototype), data(Data) {}
CScriptVarIntegerBase::~CScriptVarIntegerBase() {}
bool CScriptVarIntegerBase::isNumeric() {return true; }
int CScriptVarIntegerBase::getInt() {return data; }
bool CScriptVarIntegerBase::getBool() {return data!=0;}
double CScriptVarIntegerBase::getDouble() {return data;}
string CScriptVarIntegerBase::getString() {return int2string(data);}
string CScriptVarIntegerBase::getVarType() { return "number"; }
CScriptVarPtr CScriptVarIntegerBase::getNumericVar() { return this; }


////////////////////////////////////////////////////////////////////////// CScriptVarInteger

CScriptVarInteger::CScriptVarInteger(CTinyJS *Context, int Data) : CScriptVarIntegerBase(Context, Context->numberPrototype, Data) {}
CScriptVarInteger::~CScriptVarInteger() {}
CScriptVarPtr CScriptVarInteger::clone() { return new CScriptVarInteger(*this); }
bool CScriptVarInteger::isNumber() { return true; }
bool CScriptVarInteger::isInt() { return true; }
static char *tiny_ltoa(long val, unsigned radix) {
	char *buf, *buf_end, *p, *firstdig, temp;
	unsigned digval;

	buf = (char*)malloc(64);
	if(!buf) return 0;
	buf_end = buf+64-1; // -1 for '\0'
	
	p = buf;
	if (val < 0) {
		*p++ = '-';
		val = -val;
	}

	do {
		digval = (unsigned) (val % radix);
		val /= radix;
		*p++ = (char) (digval + (digval > 9 ? ('a'-10) : '0'));
		if(p==buf_end) {
			char *new_buf = (char *)realloc(buf, buf_end-buf+16+1); // for '\0'
			if(!new_buf) { free(buf); return 0; }
			p = new_buf + (buf_end - buf);
			buf_end = p + 16;
			buf = new_buf;
		}
	} while (val > 0);

	// We now have the digit of the number in the buffer, but in reverse
	// order.  Thus we reverse them now.
	*p-- = '\0';
	firstdig = buf;
	do	{
		temp = *p;
		*p = *firstdig;
		*firstdig = temp;
		p--;
		firstdig++;
	} while (firstdig < p);
	return buf;
}


CScriptVarPtr CScriptVarInteger::_toString(bool execute, int radix) {
	if(2 <= radix && radix <= 36)
		;
	else
		radix = 10; // todo error;
	char *str = tiny_ltoa(data, radix);
	if(str) {
		CScriptVarPtr val = newScriptVar(str);
		free(str);
		return val;
	}
	return constScriptVar(Undefined); // TODO throw Error
}


////////////////////////////////////////////////////////////////////////// CScriptVarBool

CScriptVarBool::CScriptVarBool(CTinyJS *Context, bool Data) : CScriptVarIntegerBase(Context, Context->booleanPrototype, Data?1:0) {}
CScriptVarBool::~CScriptVarBool() {}
CScriptVarPtr CScriptVarBool::clone() { return new CScriptVarBool(*this); }
bool CScriptVarBool::isBool() { return true; }
string CScriptVarBool::getString() {return data!=0?"true":"false";}
string CScriptVarBool::getVarType() { return "boolean"; }
CScriptVarPtr CScriptVarBool::getNumericVar() { return newScriptVar(data); }


////////////////////////////////////////////////////////////////////////// CScriptVarInfinity

Infinity InfinityPositive(1);
Infinity InfinityNegative(-1);
CScriptVarInfinity::CScriptVarInfinity(CTinyJS *Context, int Data) : CScriptVarIntegerBase(Context, Context->numberPrototype, Data<0?-1:1) {}
CScriptVarInfinity::~CScriptVarInfinity() {}
CScriptVarPtr CScriptVarInfinity::clone() { return new CScriptVarInfinity(*this); }
int CScriptVarInfinity::isInfinity() { return data; }
string CScriptVarInfinity::getString() {return data<0?"-Infinity":"Infinity";}


////////////////////////////////////////////////////////////////////////// CScriptVarDouble

CScriptVarDouble::CScriptVarDouble(CTinyJS *Context, double Data) : CScriptVar(Context, Context->numberPrototype), data(Data) {}
CScriptVarDouble::~CScriptVarDouble() {}
CScriptVarPtr CScriptVarDouble::clone() { return new CScriptVarDouble(*this); }
bool CScriptVarDouble::isDouble() { return true; }
bool CScriptVarDouble::isNumber() { return true; }
bool CScriptVarDouble::isNumeric() {return true; }

int CScriptVarDouble::getInt() {return (int)data; }
bool CScriptVarDouble::getBool() {return data!=0.0;}
double CScriptVarDouble::getDouble() {return data;}
string CScriptVarDouble::getString() {return float2string(data);}
string CScriptVarDouble::getVarType() { return "number"; }
CScriptVarPtr CScriptVarDouble::getNumericVar() { return this; }

static char *tiny_dtoa(double val, unsigned radix) {
	char *buf, *buf_end, *p, temp;
	unsigned digval;

	buf = (char*)malloc(64);
	if(!buf) return 0;
	buf_end = buf+64-2; // -1 for '.' , -1 for '\0'

	p = buf;
	if (val < 0.0) {
		*p++ = '-';
		val = -val;
	}

	double val_1 = floor(val);
	double val_2 = val - val_1;


	do {
		double tmp = val_1 / radix;
		val_1 = floor(tmp);
		digval = (unsigned)((tmp - val_1) * radix);

		*p++ = (char) (digval + (digval > 9 ? ('a'-10) : '0'));
		if(p==buf_end) {
			char *new_buf = (char *)realloc(buf, buf_end-buf+16+2); // +2 for '.' + '\0'
			if(!new_buf) { free(buf); return 0; }
			p = new_buf + (buf_end - buf);
			buf_end = p + 16;
			buf = new_buf;
		}
	} while (val_1 > 0.0);

	// We now have the digit of the number in the buffer, but in reverse
	// order.  Thus we reverse them now.
	char *p1 = buf;
	char *p2 = p-1;
	do	{
		temp = *p2;
		*p2-- = *p1;
		*p1++ = temp;
	} while (p1 < p2);

	if(val_2) {
		*p++ = '.';
		do {
			val_2 *= radix;
			digval = (unsigned)(val_2);
			val_2 -= digval;

			*p++ = (char) (digval + (digval > 9 ? ('a'-10) : '0'));
			if(p==buf_end) {
				char *new_buf = (char *)realloc(buf, buf_end-buf+16);
				if(!new_buf) { free(buf); return 0; }
				p = new_buf + (buf_end - buf);
				buf_end = p + 16;
				buf = new_buf;
			}
		} while (val_2 > 0.0);

	}
	*p = '\0';
	return buf;
}


CScriptVarPtr CScriptVarDouble::_toString(bool execute, int radix) {
	if(2 <= radix && radix <= 36)
		;
	else
		radix = 10; // todo error;
	if(radix == 10)
		return newScriptVar(float2string(data));
	else {
		char *str = tiny_dtoa(data, radix);
		if(str) {
			CScriptVarPtr val = newScriptVar(str);
			free(str);
			return val;
		}
		return constScriptVar(Undefined); // TODO throw Error
	}
}


////////////////////////////////////////////////////////////////////////// CScriptVarFunction

CScriptVarFunction::CScriptVarFunction(CTinyJS *Context, CScriptTokenDataFnc *Data) : CScriptVar(Context, Context->functionPrototype), data(0) { 
	setFunctionData(Data); 
}
CScriptVarFunction::~CScriptVarFunction() { setFunctionData(0); }
CScriptVarPtr CScriptVarFunction::clone() { return new CScriptVarFunction(*this); }
bool CScriptVarFunction::isFunction() { return true; }
bool CScriptVarFunction::isPrimitive()	{ return false; } 

string CScriptVarFunction::getString() {return "[ Function ]";}
string CScriptVarFunction::getVarType() { return "function"; }
string CScriptVarFunction::getParsableBlockString(TOKEN_VECT::iterator &it, TOKEN_VECT::iterator end, const string indentString, const string indent) {
	ostringstream destination;
	string nl = indent.size() ? "\n" : " ";
	string my_indentString = indentString;
	bool add_nl=false, block_start=false;

	for(; it != end; ++it) {

		string OutString;
		if(add_nl) OutString.append(nl).append(my_indentString);
		bool old_block_start = block_start;
		add_nl = block_start = false;
		if(LEX_TOKEN_DATA_STRING(it->token))
			OutString.append(it->String()).append(" ");
		else if(LEX_TOKEN_DATA_FLOAT(it->token))
			OutString.append(float2string(it->Float())).append(" ");
		else if(it->token == LEX_INT)
			OutString.append(int2string(it->Int())).append(" ");
		else if(LEX_TOKEN_DATA_FUNCTION(it->token)) {
			OutString.append("function ");
			if(it->token == LEX_R_FUNCTION)
				OutString.append(data->name);
			OutString.append("(");
			if(data->arguments.size()) {
				OutString.append(data->arguments.front());
				for(vector<string>::iterator it=data->arguments.begin()+1; it!=data->arguments.end(); ++it)
					OutString.append(", ").append(*it);
			}
			OutString.append(") ");
			TOKEN_VECT::iterator it=data->body.begin();
			OutString += getParsableBlockString(it, data->body.end(), indentString, indent);

		} else if(it->token == '{') {
			OutString.append("{");
			my_indentString.append(indent);
			add_nl = block_start = true;
		} else if(it->token == '}') {
			my_indentString.resize(my_indentString.size() - min(my_indentString.size(),indent.size()));
			if(old_block_start) 
				OutString =  "}";
			else
				OutString = nl + my_indentString + "}";
			add_nl = true;
		} else if(it->token == LEX_T_SKIP) {
			// ignore SKIP-Token
		} else {
			OutString.append(CScriptLex::getTokenStr(it->token));
			if(it->token==';') add_nl=true; else OutString.append(" ");
		}
		destination << OutString;
	}
	return destination.str();
}

string CScriptVarFunction::getParsableString(const string &indentString, const string &indent) {
	ostringstream destination;
	string nl = indent.size() ? "\n" : " ";

	destination << "function "<<data->name<<"(";

	// get list of parameters
	if(data->arguments.size()) {
		destination << data->arguments.front();
		for(vector<string>::iterator it = data->arguments.begin()+1; it != data->arguments.end(); ++it) {
			destination << ", " << *it;
		}
	}

	// add function body
	destination << ") ";


	if(isNative()) {
		destination << "{ /* native Code */ }";
	} else {
		TOKEN_VECT::iterator it=data->body.begin();
		destination << getParsableBlockString(it, data->body.end(), indentString, indent);
	}
	return destination.str();
}
CScriptTokenDataFnc *CScriptVarFunction::getFunctionData() { return data; }

void CScriptVarFunction::setFunctionData(CScriptTokenDataFnc *Data) {
	if(data) { data->unref(); data = 0; }
	if(Data) { 
		data = Data; data->ref(); 
		addChildNoDup("length", newScriptVar((int)data->arguments.size()), 0);
	}
}


////////////////////////////////////////////////////////////////////////// CScriptVarFunctionNative

CScriptVarFunctionNative::~CScriptVarFunctionNative() {}
bool CScriptVarFunctionNative::isNative() { return true; }
string CScriptVarFunctionNative::getString() {return "[ Function Native ]";}


////////////////////////////////////////////////////////////////////////// CScriptVarFunctionNativeCallback

CScriptVarFunctionNativeCallback::~CScriptVarFunctionNativeCallback() {}
CScriptVarPtr CScriptVarFunctionNativeCallback::clone() { return new CScriptVarFunctionNativeCallback(*this); }
void CScriptVarFunctionNativeCallback::callFunction(const CFunctionsScopePtr &c) { jsCallback(c, jsUserData); }


////////////////////////////////////////////////////////////////////////// CScriptVarScope


declare_dummy_t(Scope);
CScriptVarScope::~CScriptVarScope() {}
CScriptVarPtr CScriptVarScope::clone() { return CScriptVarPtr(); }
bool CScriptVarScope::isObject() { return false; }
CScriptVarPtr CScriptVarScope::scopeVar() { return this; }	///< to create var like: var a = ...
CScriptVarPtr CScriptVarScope::scopeLet() { return this; }	///< to create var like: let a = ...
CScriptVarLink *CScriptVarScope::findInScopes(const string &childName) { 
	return  CScriptVar::findChild(childName); 
}
CScriptVarScopePtr CScriptVarScope::getParent() { return CScriptVarScopePtr(); } ///< no Parent


////////////////////////////////////////////////////////////////////////// CScriptVarScopeFnc

declare_dummy_t(ScopeFnc);
CScriptVarScopeFnc::~CScriptVarScopeFnc() {}
CScriptVarLink *CScriptVarScopeFnc::findInScopes(const string &childName) { 
	CScriptVarLink * ret = findChild(childName); 
	if( !ret ) {
		if(closure) ret = CScriptVarScopePtr(closure)->findInScopes(childName);
		else ret = context->getRoot()->findChild(childName);
	}
	return ret;
}

void CScriptVarScopeFnc::setReturnVar(const CScriptVarPtr &var) {
	addChildNoDup(TINYJS_RETURN_VAR, var);
}

CScriptVarPtr CScriptVarScopeFnc::getParameter(const string &name) {
	return getArgument(name);
}

CScriptVarPtr CScriptVarScopeFnc::getParameter(int Idx) {
	return getArgument(Idx);
}
CScriptVarPtr CScriptVarScopeFnc::getArgument(const string &name) {
	return findChildOrCreate(name);
}
CScriptVarPtr CScriptVarScopeFnc::getArgument(int Idx) {
	CScriptVarLink *arguments = findChildOrCreate(TINYJS_ARGUMENTS_VAR);
	if(arguments) arguments = (*arguments)->findChild(int2string(Idx));
	return arguments ? arguments : constScriptVar(Undefined);
}
int CScriptVarScopeFnc::getParameterLength() {
	return getArgumentsLength();
}
int CScriptVarScopeFnc::getArgumentsLength() {
	CScriptVarLink *arguments = findChild(TINYJS_ARGUMENTS_VAR);
	if(arguments) arguments = (*arguments)->findChild("length");
	return arguments ? (*arguments)->getInt() : 0;
}



////////////////////////////////////////////////////////////////////////// CScriptVarScopeLet

declare_dummy_t(ScopeLet);
CScriptVarScopeLet::CScriptVarScopeLet(const CScriptVarScopePtr &Parent) // constructor for LetScope
	: CScriptVarScope(Parent->getContext()), parent(addChild(TINYJS_SCOPE_PARENT_VAR, Parent, 0)) {}

CScriptVarScopeLet::~CScriptVarScopeLet() {}
CScriptVarPtr CScriptVarScopeLet::scopeVar() {						// to create var like: var a = ...
	return getParent()->scopeVar(); 
}
CScriptVarScopePtr CScriptVarScopeLet::getParent() { return (CScriptVarPtr)parent; }
CScriptVarLink *CScriptVarScopeLet::findInScopes(const string &childName) { 
	CScriptVarLink *ret = findChild(childName); 
	if( !ret ) ret = getParent()->findInScopes(childName);
	return ret;
}

////////////////////////////////////////////////////////////////////////// CScriptVarScopeWith

declare_dummy_t(ScopeWith);
CScriptVarScopeWith::~CScriptVarScopeWith() {}
CScriptVarPtr CScriptVarScopeWith::scopeLet() { 							// to create var like: let a = ...
	return getParent()->scopeLet();
}
CScriptVarLink *CScriptVarScopeWith::findInScopes(const string &childName) { 
	if(childName == "this") return with;
	CScriptVarLink * ret = (*with)->findChild(childName); 
	if( !ret ) ret = getParent()->findInScopes(childName);
	return ret;
}

// ----------------------------------------------------------------------------------- CSCRIPT
bool CTinyJS::noexecute = false; 
CTinyJS::CTinyJS() {
	CScriptVarPtr var, prototype;
	t = 0;
	runtimeFlags = 0;
	first = 0;
	uniqueID = 0;

	
	//////////////////////////////////////////////////////////////////////////
	// Object-Prototype
	// must be created as first object because this prototype is the base of all objects
	objectPrototype = newScriptVar(Object); 
	
	// all objects have a prototype. Also the prototype of prototypes
	objectPrototype->addChild(TINYJS___PROTO___VAR, objectPrototype, 0);

	//////////////////////////////////////////////////////////////////////////
	// Function-Prototype
	// must be created as second object because this is the base of all functions (also constructors)
	functionPrototype = newScriptVar(Object);


	//////////////////////////////////////////////////////////////////////////
	// Scopes
	root = ::newScriptVar(this, Scope);
	scopes.push_back(root);

	//////////////////////////////////////////////////////////////////////////
	// Add built-in classes
	//////////////////////////////////////////////////////////////////////////
	// Object
	var = addNative("function Object()", this, &CTinyJS::native_Object);
	objectPrototype = var->findChild(TINYJS_PROTOTYPE_CLASS);
	addNative("function Object.getPrototypeOf(obj)", this, &CTinyJS::native_Object_getPrototypeOf); 
	addNative("function Object.prototype.hasOwnProperty(prop)", this, &CTinyJS::native_Object_hasOwnProperty); 
	objectPrototype_valueOf = addNative("function Object.prototype.valueOf()", this, &CTinyJS::native_Object_valueOf); 
	objectPrototype_toString = addNative("function Object.prototype.toString(radix)", this, &CTinyJS::native_Object_toString); 
	pseudo_statics.push_back(&objectPrototype);
	pseudo_statics.push_back(&objectPrototype_valueOf);
	pseudo_statics.push_back(&objectPrototype_toString);

	//////////////////////////////////////////////////////////////////////////
	// Array
	var = addNative("function Array()", this, &CTinyJS::native_Array);
	arrayPrototype = var->findChild(TINYJS_PROTOTYPE_CLASS);
	arrayPrototype->addChild("valueOf", objectPrototype_valueOf);
	arrayPrototype->addChild("toString", objectPrototype_toString);
	pseudo_statics.push_back(&arrayPrototype);

	//////////////////////////////////////////////////////////////////////////
	// String
	var = addNative("function String()", this, &CTinyJS::native_String);
	stringPrototype  = var->findChild(TINYJS_PROTOTYPE_CLASS);
	stringPrototype->addChild("valueOf", objectPrototype_valueOf);
	stringPrototype->addChild("toString", objectPrototype_toString);
	pseudo_statics.push_back(&stringPrototype);

	//////////////////////////////////////////////////////////////////////////
	// Number
	var = addNative("function Number()", this, &CTinyJS::native_Number);
	var->addChild("NaN", constNaN = newScriptVarNaN(this), SCRIPTVARLINK_ENUMERABLE);
	var->addChild("POSITIVE_INFINITY", constInfinityPositive = newScriptVarInfinity(this, InfinityPositive), SCRIPTVARLINK_ENUMERABLE);
	var->addChild("NEGATIVE_INFINITY", constInfinityNegative = newScriptVarInfinity(this, InfinityNegative), SCRIPTVARLINK_ENUMERABLE);
	numberPrototype = var->findChild(TINYJS_PROTOTYPE_CLASS);
	numberPrototype->addChild("valueOf", objectPrototype_valueOf);
	numberPrototype->addChild("toString", objectPrototype_toString);
	pseudo_statics.push_back(&numberPrototype);
	pseudo_statics.push_back(&constNaN);
	pseudo_statics.push_back(&constInfinityPositive);
	pseudo_statics.push_back(&constInfinityNegative);

	//////////////////////////////////////////////////////////////////////////
	// Boolean
	var = addNative("function Boolean()", this, &CTinyJS::native_Boolean);
	booleanPrototype = var->findChild(TINYJS_PROTOTYPE_CLASS);
	booleanPrototype->addChild("valueOf", objectPrototype_valueOf);
	booleanPrototype->addChild("toString", objectPrototype_toString);
	pseudo_statics.push_back(&booleanPrototype);

	//////////////////////////////////////////////////////////////////////////
	// Function
	var = addNative("function Function(params, body)", this, &CTinyJS::native_Function); 
	var->addChildNoDup(TINYJS_PROTOTYPE_CLASS, functionPrototype);
	addNative("function Function.prototype.call(objc)", this, &CTinyJS::native_Function_call); 
	addNative("function Function.prototype.apply(objc, args)", this, &CTinyJS::native_Function_apply); 
	functionPrototype->addChild("valueOf", objectPrototype_valueOf);
	functionPrototype->addChild("toString", objectPrototype_toString);
	pseudo_statics.push_back(&functionPrototype);

	//////////////////////////////////////////////////////////////////////////
	// Error
	var = addNative("function Error(message, fileName, lineNumber, column)", this, &CTinyJS::native_Error); 
	errorPrototype = var->findChild(TINYJS_PROTOTYPE_CLASS);
	errorPrototype->addChild("message", newScriptVar(""));
	errorPrototype->addChild("name", newScriptVar("Error"));
	errorPrototype->addChild("fileName", newScriptVar(""));
	errorPrototype->addChild("lineNumber", newScriptVar(-1));	// -1 means not viable
	errorPrototype->addChild("column", newScriptVar(-1));			// -1 means not viable
	pseudo_statics.push_back(&errorPrototype);

	var = addNative("function EvalError(message, fileName, lineNumber, column)", this, &CTinyJS::native_EvalError); 
	prototype = var->findChild(TINYJS_PROTOTYPE_CLASS);
	prototype->addChildNoDup(TINYJS___PROTO___VAR, errorPrototype);
	prototype->addChild("name", newScriptVar("EvalError"));

	var = addNative("function RangeError(message, fileName, lineNumber, column)", this, &CTinyJS::native_RangeError); 
	prototype = var->findChild(TINYJS_PROTOTYPE_CLASS);
	prototype->addChildNoDup(TINYJS___PROTO___VAR, errorPrototype);
	prototype->addChild("name", newScriptVar("RangeError"));

	var = addNative("function ReferenceError(message, fileName, lineNumber, column)", this, &CTinyJS::native_ReferenceError); 
	prototype = var->findChild(TINYJS_PROTOTYPE_CLASS);
	prototype->addChildNoDup(TINYJS___PROTO___VAR, errorPrototype);
	prototype->addChild("name", newScriptVar("ReferenceError"));

	var = addNative("function SyntaxError(message, fileName, lineNumber, column)", this, &CTinyJS::native_SyntaxError); 
	prototype = var->findChild(TINYJS_PROTOTYPE_CLASS);
	prototype->addChildNoDup(TINYJS___PROTO___VAR, errorPrototype);
	prototype->addChild("name", newScriptVar("SyntaxError"));

	var = addNative("function TypeError(message, fileName, lineNumber, column)", this, &CTinyJS::native_TypeError); 
	prototype = var->findChild(TINYJS_PROTOTYPE_CLASS);
	prototype->addChildNoDup(TINYJS___PROTO___VAR, errorPrototype);
	prototype->addChild("name", newScriptVar("TypeError"));

	//////////////////////////////////////////////////////////////////////////
	// add global built-in vars & constants
	root->addChild("undefined", constUndefined = newScriptVarUndefined(this), SCRIPTVARLINK_ENUMERABLE);
	pseudo_statics.push_back(&constUndefined);
	root->addChild("NaN", constNaN, SCRIPTVARLINK_ENUMERABLE);
	root->addChild("Infinity", constInfinityPositive, SCRIPTVARLINK_ENUMERABLE);
	constFalse	= newScriptVarBool(this, false);	pseudo_statics.push_back(&constFalse);
	constTrue	= newScriptVarBool(this, true);	pseudo_statics.push_back(&constTrue);
	constZero	= newScriptVar(0);					pseudo_statics.push_back(&constZero);
	constOne		= newScriptVar(1);					pseudo_statics.push_back(&constOne);
	//////////////////////////////////////////////////////////////////////////
	// add global functions
	addNative("function eval(jsCode)", this, &CTinyJS::native_eval); // execute the given string and return the result
	addNative("function isNaN(objc)", this, &CTinyJS::native_isNAN);
	addNative("function isFinite(objc)", this, &CTinyJS::native_isFinite);
	addNative("function parseInt(string, radix)", this, &CTinyJS::native_parseInt);
	addNative("function parseFloat(string)", this, &CTinyJS::native_parseFloat);
	
	
	addNative("function JSON.parse(text, reviver)", this, &CTinyJS::native_JSON_parse);
	
}

CTinyJS::~CTinyJS() {
	ASSERT(!t);
	for(vector<CScriptVarPtr*>::iterator it = pseudo_statics.begin(); it!=pseudo_statics.end(); ++it)
		**it = CScriptVarPtr();
	root->removeAllChildren();
	scopes.clear();
	ClearLostVars();
	root = CScriptVarPtr();
#ifdef _DEBUG
	for(CScriptVar *p = first; p; p=p->next)
		printf("%p\n", p);
#endif
#if DEBUG_MEMORY
	show_allocated();
#endif
}

void CTinyJS::throwError(bool &execute, const string &message) {
	if(execute && (runtimeFlags & RUNTIME_CANTHROW)) {
		exceptionVar = newScriptVar(message);
		runtimeFlags |= RUNTIME_THROW;
		execute = false;
		return;
	}
	throw new CScriptException(message, t->currentFile, t->currentLine(), t->currentColumn());
}

void CTinyJS::throwError(bool &execute, const string &message, CScriptTokenizer::ScriptTokenPosition &Pos) {
	if(execute && (runtimeFlags & RUNTIME_CANTHROW)) {
		exceptionVar = newScriptVar(message);
		runtimeFlags |= RUNTIME_THROW;
		execute = false;
		return;
	}
	throw new CScriptException(message, t->currentFile, Pos.currentLine(), Pos.currentColumn());
}

void CTinyJS::trace() {
	root->trace();
}

void CTinyJS::execute(CScriptTokenizer &Tokenizer) {
	evaluateComplex(Tokenizer);
}

void CTinyJS::execute(const char *Code, const string &File, int Line, int Column) {
	evaluateComplex(Code, File, Line, Column);
}

void CTinyJS::execute(const string &Code, const string &File, int Line, int Column) {
	evaluateComplex(Code, File, Line, Column);
}

CScriptVarLink CTinyJS::evaluateComplex(CScriptTokenizer &Tokenizer) {
	CScriptVarLinkPtr v;
	t = &Tokenizer;
	try {
		bool execute = true;
		do {
			v = execute_statement(execute);
			while (t->tk==';') t->match(';'); // skip empty statements
		} while (t->tk!=LEX_EOF);
	} catch (CScriptException *e) {
		runtimeFlags = 0; // clean up runtimeFlags
		ostringstream msg;
		msg << "Error " << e->text;
		if(e->line >= 0) msg << " at Line:" << e->line+1 << " Column:" << e->column+1;
		if(e->file.length()) msg << " in " << e->file;
		delete e;
		t=0;
		throw new CScriptException(msg.str(),"");
	}
	t=0;
	
	ClearLostVars(v);

	uint32_t UniqueID = getUniqueID(); 
	setTemporaryID_recursive(UniqueID);
	if(v) v->getVarPtr()->setTemporaryID_recursive(UniqueID);
	for(CScriptVar *p = first; p; p=p->next)
	{
		if(p->temporaryID != UniqueID)
			printf("%p\n", p);
	}

	if (v) {
		return CScriptVarLink(v->getVarPtr());
	}
	// return undefined...
	return CScriptVarLink(constScriptVar(Undefined));
}
CScriptVarLink CTinyJS::evaluateComplex(const char *Code, const string &File, int Line, int Column) {
	CScriptTokenizer Tokenizer(Code, File, Line, Column);
	return evaluateComplex(Tokenizer);
}
CScriptVarLink CTinyJS::evaluateComplex(const string &Code, const string &File, int Line, int Column) {
	CScriptTokenizer Tokenizer(Code.c_str(), File, Line, Column);
	return evaluateComplex(Tokenizer);
}

string CTinyJS::evaluate(CScriptTokenizer &Tokenizer) {
	return evaluateComplex(Tokenizer).getVarPtr()->getString();
}
string CTinyJS::evaluate(const char *Code, const string &File, int Line, int Column) {
	return evaluateComplex(Code, File, Line, Column).getVarPtr()->getString();
}
string CTinyJS::evaluate(const string &Code, const string &File, int Line, int Column) {
	return evaluate(Code.c_str(), File, Line, Column);
}

CScriptVarFunctionNativePtr CTinyJS::addNative(const string &funcDesc, JSCallback ptr, void *userdata) {
	return addNative(funcDesc, ::newScriptVar(this, ptr, userdata));
}

CScriptVarFunctionNativePtr CTinyJS::addNative(const string &funcDesc, CScriptVarFunctionNativePtr Var) {
	CScriptLex lex(funcDesc.c_str());
	CScriptVarPtr base = root;

	lex.match(LEX_R_FUNCTION);
	string funcName = lex.tkStr;
	lex.match(LEX_ID);
	/* Check for dots, we might want to do something like function String.substring ... */
	while (lex.tk == '.') {
		lex.match('.');
		CScriptVarLink *link = base->findChild(funcName);
		// if it doesn't exist, make an object class
		if (!link) link = base->addChild(funcName, newScriptVar(Object));
		base = link->getVarPtr();
		funcName = lex.tkStr;
		lex.match(LEX_ID);
	}

	auto_ptr<CScriptTokenDataFnc> pFunctionData(new CScriptTokenDataFnc);
	lex.match('(');
	while (lex.tk!=')') {
		pFunctionData->arguments.push_back(lex.tkStr);
		lex.match(LEX_ID);
		if (lex.tk!=')') lex.match(',');
	}
	lex.match(')');
	Var->setFunctionData(pFunctionData.release());
	Var->addChild(TINYJS_PROTOTYPE_CLASS, newScriptVar(Object), SCRIPTVARLINK_WRITABLE);

	base->addChild(funcName,  Var);
	return Var;

}

CScriptVarLinkPtr CTinyJS::parseFunctionDefinition(CScriptToken &FncToken) {
	CScriptTokenDataFnc &Fnc = FncToken.Fnc();
	string fncName = (FncToken.token == LEX_T_FUNCTION_OPERATOR) ? TINYJS_TEMP_NAME : Fnc.name;
	CScriptVarLinkPtr funcVar(newScriptVar(&Fnc), fncName);
	if(scope() != root)
		(*funcVar)->addChild(TINYJS_FUNCTION_CLOSURE_VAR, scope(), 0);
	(*funcVar)->addChild(TINYJS_PROTOTYPE_CLASS, newScriptVar(Object), SCRIPTVARLINK_WRITABLE);
	return funcVar;
}
/*
CScriptVarSmartLink CTinyJS::parseFunctionDefinition() {
	CScriptTokenDataFnc &Fnc = t->getToken().Fnc();
	string fncName = (t->tk == LEX_T_FUNCTION_FORCE_ANONYMOUS) ? TINYJS_TEMP_NAME : Fnc.name;
	CScriptVarSmartLink funcVar = new CScriptVarLink(new CScriptVar(Fnc), fncName);
	(*funcVar)->addChild("length", new CScriptVar((int)Fnc.parameter.size()));
	return funcVar;
}
*/

CScriptVarLinkPtr CTinyJS::parseFunctionsBodyFromString(const string &Parameter, const string &FncBody) {
	string Fnc = "function ("+Parameter+"){"+FncBody+"}";
	CScriptTokenizer tokenizer(Fnc.c_str());
	return parseFunctionDefinition(tokenizer.getToken());
}

class CScriptEvalException {
public:
	int runtimeFlags;
	CScriptEvalException(int RuntimeFlags) : runtimeFlags(RuntimeFlags){}
};

CScriptVarPtr CTinyJS::callFunction(bool &execute, const CScriptVarPtr &Function, std::vector<CScriptVarPtr> &Arguments, const CScriptVarPtr &This, CScriptVarPtr *newThis) {
//CScriptVarSmartLink CTinyJS::callFunction(CScriptVarSmartLink &Function, vector<CScriptVarSmartLink> &Arguments, const CScriptVarPtr& This, bool &execute) {
	ASSERT(Function->isFunction());

	CScriptVarScopeFncPtr functionRoot(::newScriptVar(this, ScopeFnc, CScriptVarPtr(Function->findChild(TINYJS_FUNCTION_CLOSURE_VAR))));
	functionRoot->addChild("this", This);
	CScriptVarPtr arguments = functionRoot->addChild(TINYJS_ARGUMENTS_VAR, newScriptVar(Object));
	CScriptTokenDataFnc *Fnc = Function->getFunctionData();
	if(Fnc->name.size()) functionRoot->addChild(Fnc->name, Function);

	int length_proto = Fnc->arguments.size();
	int length_arguments = Arguments.size();
	int length = max(length_proto, length_arguments);
	for(int arguments_idx = 0; arguments_idx<length; ++arguments_idx) {
		string arguments_idx_str = int2string(arguments_idx);
		CScriptVarLinkPtr value;
		if(arguments_idx < length_arguments) {
			value = arguments->addChild(arguments_idx_str, Arguments[arguments_idx]);
		} else {
			value = constScriptVar(Undefined);
		}
		if(arguments_idx < length_proto) {
			functionRoot->addChildNoDup(Fnc->arguments[arguments_idx], value);
		}
	}
	arguments->addChild("length", newScriptVar(length_arguments));
	CScriptVarLinkPtr returnVar;

	int old_function_runtimeFlags = runtimeFlags; // save runtimeFlags
	runtimeFlags &= ~RUNTIME_LOOP_MASK; // clear LOOP-Flags
	// execute function!
	// add the function's execute space to the symbol table so we can recurse
	// scopes.push_back(SCOPES::SCOPE_TYPE_FUNCTION, functionRoot->get());
	CScopeControl ScopeControl(this);
	ScopeControl.addFncScope(functionRoot);
	if (Function->isNative()) {
		try {
			CScriptVarFunctionNativePtr(Function)->callFunction(functionRoot);
//				((CScriptVarFunctionNative*)*Function.getVar())->callFunction(functionRoot.getVar());
			runtimeFlags = old_function_runtimeFlags | (runtimeFlags & RUNTIME_THROW); // restore runtimeFlags
			if(runtimeFlags & RUNTIME_THROW) {
				execute = false;
			}
		} catch (CScriptEvalException *e) {
			int exeption_runtimeFlags = e->runtimeFlags;
			delete e;
			runtimeFlags = old_function_runtimeFlags; // restore runtimeFlags
			if(exeption_runtimeFlags & RUNTIME_BREAK) {
				if(runtimeFlags & RUNTIME_CANBREAK) {
					runtimeFlags |= RUNTIME_BREAK;
					execute = false;
				}
				else
					throw new CScriptException("'break' must be inside loop or switch");
			} else if(exeption_runtimeFlags & RUNTIME_CONTINUE) {
				if(runtimeFlags & RUNTIME_CANCONTINUE) {
					runtimeFlags |= RUNTIME_CONTINUE;
					execute = false;
				}
				else
					throw new CScriptException("'continue' must be inside loop");
			}
		} catch (CScriptVarPtr v) {
			if(runtimeFlags & RUNTIME_CANTHROW) {
				runtimeFlags |= RUNTIME_THROW;
				execute = false;
				exceptionVar = v;
			}
			else {
				string e = "uncaught exception: '"+v->getString()+"' in: "+Function->getFunctionData()->name+"()";
				throw new CScriptException(e);
			}
		}
	} else {
		/* we just want to execute the block, but something could
			* have messed up and left us with the wrong ScriptLex, so
			* we want to be careful here... */
		string oldFile = t->currentFile;
		t->currentFile = Fnc->file;
		t->pushTokenScope(Fnc->body);
		SET_RUNTIME_CANRETURN;
		execute_block(execute);
		t->currentFile = oldFile;

		// because return will probably have called this, and set execute to false
		runtimeFlags = old_function_runtimeFlags | (runtimeFlags & RUNTIME_THROW); // restore runtimeFlags
		if(!(runtimeFlags & RUNTIME_THROW)) {
			execute = true;
		}
	}
	if(execute && newThis)
		*newThis = functionRoot->findChild("this");
	/* get the real return var before we remove it from our function */
	if(execute && (returnVar = functionRoot->findChild(TINYJS_RETURN_VAR)))
		return returnVar;
	else
		return constScriptVar(Undefined);
}

#define RETURN_NAN return constScriptVar(NaN)





CScriptVarPtr CTinyJS::mathsOp(bool &execute, const CScriptVarPtr &A, const CScriptVarPtr &B, int op) {
	if(!execute) return constUndefined;
	if (op == LEX_TYPEEQUAL || op == LEX_NTYPEEQUAL) {
		// check type first, then call again to check data
		if(A->isNaN() || B->isNaN()) return constFalse;
		if( (typeid(*A.getVar()) == typeid(*B.getVar())) ^ (op != LEX_TYPEEQUAL) )
			return mathsOp(execute, A, B, op == LEX_TYPEEQUAL ? LEX_EQUAL : LEX_NEQUAL);
		return constFalse;
	}
	if (!A->isPrimitive() && !B->isPrimitive()) { // Objects
		/* Just check pointers */
		switch (op) {
		case LEX_EQUAL:	return constScriptVar(A==B);
		case LEX_NEQUAL:	return constScriptVar(A!=B);
		}
	}

	CScriptVarPtr a = A->getPrimitivVar(execute);
	CScriptVarPtr b = B->getPrimitivVar(execute);
	if(!execute) return constUndefined;
	// do maths...
	bool a_isString = a->isString();
	bool b_isString = b->isString();
	// special for strings and string '+'
	// both a String or one a String and op='+'
	if( (a_isString && b_isString) || ((a_isString || b_isString) && op == '+')) {
		string da = a->getString();
		string db = b->getString();
		switch (op) {
		case '+':			return newScriptVar(da+db);
		case LEX_EQUAL:	return constScriptVar(da==db);
		case LEX_NEQUAL:	return constScriptVar(da!=db);
		case '<':			return constScriptVar(da<db);
		case LEX_LEQUAL:	return constScriptVar(da<=db);
		case '>':			return constScriptVar(da>db);
		case LEX_GEQUAL:	return constScriptVar(da>=db);
		default:				RETURN_NAN;
		}
	}
	// special for undefined and null
	else if( (a->isUndefined() || a->isNull()) && (b->isUndefined() || b->isNull()) ) {
		switch (op) {
		case LEX_NEQUAL:	return constScriptVar( !( ( a->isUndefined() || a->isNull() ) && ( b->isUndefined() || b->isNull() ) ) );
		case LEX_EQUAL:	return constScriptVar(    ( a->isUndefined() || a->isNull() ) && ( b->isUndefined() || b->isNull() )   );
		case LEX_GEQUAL:	
		case LEX_LEQUAL:
		case '<':
		case '>':			return constScriptVar(false);
		default:				RETURN_NAN;
		}
	}

	// gets only an Integer, a Double, in Infinity or a NaN
	CScriptVarPtr a_l = a->getNumericVar();
	CScriptVarPtr b_l = b->getNumericVar();
	{
		CScriptVarPtr a = a_l;
		CScriptVarPtr b = b_l;

		if( a->isNaN() || b->isNaN() ) {
			switch (op) {
			case LEX_NEQUAL:	return constScriptVar(true);
			case LEX_EQUAL:
			case LEX_GEQUAL:	
			case LEX_LEQUAL:
			case '<':
			case '>':			return constScriptVar(false);
			default:				RETURN_NAN;
			}
		}
		else if((a->isInfinity() || b->isInfinity())) {
			int tmp = 0;
			int a_i=a->isInfinity(), a_sig=a->getInt()>0?1:-1;
			int b_i=b->isInfinity(), b_sig=a->getInt()>0?1:-1;
			switch (op) {
			case LEX_EQUAL:	return constScriptVar(a_i == b_i);
			case LEX_GEQUAL:	
			case '>':			return constScriptVar(a_i >= b_i);
			case LEX_LEQUAL:	
			case '<':			return constScriptVar(a_i <= b_i);
			case LEX_NEQUAL:	return constScriptVar(a_i != b_i);
			case '+':			if(a_i && b_i && a_i != b_i) RETURN_NAN;
				return constScriptVar(Infinity(b_i?b_i:a_i));
			case '-':			if(a_i && a_i == b_i) RETURN_NAN;
				return constScriptVar(Infinity(b_i?-b_i:a_i));
			case '*':			tmp = a->getInt() * b->getInt();
				if(tmp == 0)	RETURN_NAN;
				return constScriptVar(Infinity(tmp));
			case '/':			if(a_i && b_i) RETURN_NAN;
				if(b_i)			return newScriptVar(0);
				return constScriptVar(Infinity(a_sig*b_sig));
			case '%':			if(a_i) RETURN_NAN;
				return constScriptVar(Infinity(a_sig));
			case '&':			return newScriptVar( 0);
			case '|':
			case '^':			if(a_i && b_i) return newScriptVar( 0);
				return newScriptVar(a_i?b->getInt():a->getInt());
			case LEX_LSHIFT:
			case LEX_RSHIFT:	
			case LEX_RSHIFTU:	if(a_i) return newScriptVar(0);
				return newScriptVar(a->getInt());
			default:				throw new CScriptException("This operation not supported on the int datatype");
			}
		} else {
			if (!a->isDouble() && !b->isDouble()) {
				// use ints
				return DoMaths<int>(a, b, op);
			} else {
				// use doubles
				return DoMaths<double>(a, b, op);
			}
		}
	}

	ASSERT(0);
	return constUndefined;
}

/*
CScriptVarSmartLink CTinyJS::setValue( CScriptVarSmartLink Var )
{
	if((*Var)->isAccessor()) {

	}
}

CScriptVarSmartLink CTinyJS::getValue( CScriptVarSmartLink Var, bool execute )
{
	if((*Var)->isAccessor()) {
		CScriptVarLink *get = (*Var)->findChild("__accessor_get__");
		if(get) {
		} else
			return CScriptVarSmartLink(new CScriptVar());
	} else
		return Var;
}
*/
CScriptVarLinkPtr CTinyJS::execute_literals(bool &execute) {
	if(t->tk == LEX_ID) {
		CScriptVarLinkPtr a;
		if(execute) {
			a(execute, findInScopes(t->tkStr()));
			if (!a) {
				/* Variable doesn't exist! JavaScript says we should create it
				 * (we won't add it here. This is done in the assignment operator)*/
				if(t->tkStr() == "this") {
					a = root; // fake this
				} else {
					a(constScriptVar(Undefined), t->tkStr());
				}
			} else if(t->tkStr() == "this")
				a(a->getVarPtr()); // prevent assign to this
			t->match(t->tk);
			return a;
		}
		t->match(t->tk);
	} else if (t->tk==LEX_INT) {
		CScriptVarPtr a = newScriptVar(t->getToken().Int());
		t->match(t->tk);
		return a;
	} else if (t->tk==LEX_FLOAT) {
		CScriptVarPtr a = newScriptVar(t->getToken().Float());
		t->match(t->tk);
		return a;
	} else if (t->tk==LEX_STR) {
		CScriptVarPtr a = newScriptVar(t->getToken().String());
		t->match(LEX_STR);
		return a;
	} else if (t->tk=='{') {
		if(execute) {
			CScriptVarPtr contents(newScriptVar(Object));
			/* JSON-style object definition */
			t->match('{');
			while (t->tk != '}') {
				// we only allow strings or IDs on the left hand side of an initialisation
				if (t->tk==LEX_STR || t->tk==LEX_ID) {
					string id = t->tkStr();
					t->match(t->tk);
					t->match(':');
					CScriptVarLinkPtr a = execute_assignment(execute);
					if (execute) {
						contents->addChildNoDup(id, a);
					}
				} else if(t->tk==LEX_T_GET || t->tk==LEX_T_SET) {
					CScriptTokenDataFnc &Fnc = t->getToken().Fnc();
					if((t->tk == LEX_T_GET && Fnc.arguments.size()==0) || (t->tk == LEX_T_SET && Fnc.arguments.size()==1)) {
						CScriptVarLinkPtr funcVar = parseFunctionDefinition(t->getToken());
						CScriptVarLink *child = contents->findChild(Fnc.name);
						if(child && !(*child)->isAccessor()) child = 0;
						if(!child) child = contents->addChildNoDup(Fnc.name, newScriptVar(Accessor));
						child->getVarPtr()->addChildNoDup((t->tk==LEX_T_GET?TINYJS_ACCESSOR_GET_VAR:TINYJS_ACCESSOR_SET_VAR), funcVar->getVarPtr());
					}
					t->match(t->tk);
				}
				else
					t->match(LEX_ID);
				// no need to clean here, as it will definitely be used
				if (t->tk != ',') break;
				t->match(',');
			}

			t->match('}');
			return contents;
		} else
			t->skip(t->getToken().Int());
	} else if (t->tk=='[') {
		if(execute) {
			CScriptVarPtr contents(newScriptVar(Array));
			/* JSON-style array */
			t->match('[');
			int idx = 0;
			while (t->tk != ']') {
				CScriptVarLinkPtr a = execute_assignment(execute);
				if (execute) {
					contents->addChild(int2string(idx), a);
				}
				// no need to clean here, as it will definitely be used
				if (t->tk != ']') t->match(',');
				idx++;
			}
			t->match(']');
	//		return new CScriptVarLink(contents.release());
			return contents;
		} else
			t->skip(t->getToken().Int());
	} else if (t->tk==LEX_R_LET) { // let as expression
		if(execute) {
			t->match(LEX_R_LET);
			t->match('(');
			CScopeControl ScopeControl(this);
			ScopeControl.addLetScope();
			for(;;) {
				CScriptVarLinkPtr a;
				string var = t->tkStr();
				t->match(LEX_ID);
				a = scope()->scopeLet()->findChildOrCreate(var);
				a->setDeletable(false);
				// sort out initialiser
				if (t->tk == '=') {
					t->match('=');
					a.replaceVar(execute, execute_assignment(execute));
				}
				if (t->tk == ',') 
					t->match(',');
				else
					break;
			}
			t->match(')');
			return execute_base(execute);
		} else {
			t->skip(t->getToken().Int());
			execute_base(execute);
		}
	} else if (t->tk==LEX_R_FUNCTION) {
		ASSERT(0); // no function as operator
	} else if (t->tk==LEX_T_FUNCTION_OPERATOR) {
		if(execute) {
			CScriptVarLinkPtr a = parseFunctionDefinition(t->getToken());
			t->match(t->tk);
			return a;
		}
		t->match(t->tk);
	} else if (t->tk==LEX_R_NEW) {
		// new -> create a new object
		t->match(LEX_R_NEW);
		CScriptVarLinkPtr parent = execute_literals(execute);
		CScriptVarLinkPtr objClass = execute_member(parent, execute);
		if (execute) {
			if(objClass->getVarPtr()->isFunction()) {
				CScriptVarPtr obj(newScriptVar(Object));
				CScriptVarLink *prototype = (*objClass)->findChild(TINYJS_PROTOTYPE_CLASS);
				if(!prototype) prototype = (*objClass)->addChild(TINYJS_PROTOTYPE_CLASS, newScriptVar(Object), SCRIPTVARLINK_WRITABLE);
				obj->addChildNoDup(TINYJS___PROTO___VAR, prototype, SCRIPTVARLINK_WRITABLE);
				vector<CScriptVarPtr> arguments;
				if (t->tk == '(') {
					t->match('(');
					while(t->tk!=')') {
						CScriptVarPtr value = execute_assignment(execute);
						if (execute) {
							arguments.push_back(value);
						}
						if (t->tk!=')') { t->match(','); }
					}
					t->match(')');
				}
				if(execute) {
					CScriptVarPtr returnVar = callFunction(execute, objClass->getVarPtr(), arguments, obj, &obj);
					if(!returnVar->isPrimitive())
						return CScriptVarLinkPtr(returnVar);
					return CScriptVarLinkPtr(obj);
				}
			} else {
				throwError(execute, objClass->getName() + " is not a constructor");
			}
		} else {
			if (t->tk == '(') t->skip(t->getToken().Int());
		}
	} else if (t->tk==LEX_R_TRUE) {
		t->match(LEX_R_TRUE);
		return constScriptVar(true);
	} else if (t->tk==LEX_R_FALSE) {
		t->match(LEX_R_FALSE);
		return constScriptVar(false);
	} else if (t->tk==LEX_R_NULL) {
		t->match(LEX_R_NULL);
		return newScriptVar(Null);
	} else if (t->tk=='(') {
		t->match('(');
		CScriptVarLinkPtr a = execute_base(execute);
		t->match(')');
		return a;
	} else 
		t->match(LEX_EOF);
	return constScriptVar(Undefined);

}
CScriptVarLinkPtr CTinyJS::execute_member(CScriptVarLinkPtr &parent, bool &execute) {
	CScriptVarLinkPtr a;
	parent.swap(a);
	if(t->tk == '.' || t->tk == '[') {
//		string path = a->name;
//		CScriptVar *parent = 0;
		while(t->tk == '.' || t->tk == '[') {
		
			if(execute && ((*a)->isUndefined() || (*a)->isNull())) {
				throwError(execute, a->getName() + " is " + (*a)->getString());
			}
			string name;
			if(t->tk == '.') {
				t->match('.');
				name = t->tkStr();
//				path += "."+name;
				t->match(LEX_ID);
			} else {
				t->match('[');
				CScriptVarLinkPtr index = execute_expression(execute);
				name = (*index)->getString();
//				path += "["+name+"]";
				t->match(']');
			}
			if (execute) {
				bool in_prototype = false;
				CScriptVarLink *child = (*a)->findChildWithPrototypeChain(name);
//				if ( !child && (child = findInPrototypeChain(a, name)) )
				if ( child && child->getOwner() != a->getVarPtr().getVar() ) 
					in_prototype = true;
				if (!child) {
					/* if we haven't found this defined yet, use the built-in
						'length' properly */
					if ((*a)->isArray() && name == "length") {
						int l = (*a)->getArrayLength();
						child = new CScriptVarLink(newScriptVar(l));
					} else if ((*a)->isString() && name == "length") {
						int l = (*a)->getString().size();
						child = new CScriptVarLink(newScriptVar(l));
					} else {
						//child = (*a)->addChild(name);
					}
				}
				if(child) {
					parent = a;
					if(in_prototype) {
						a(child->getVarPtr(), child->getName());
						a->setOwner(parent->getVarPtr().getVar()); // fake owner - but not set Owned -> for assignment stuff
					} else
						a = child;
				} else {
					CScriptVar *owner = a->getVarPtr().getVar();
					a(constScriptVar(Undefined), name);
					a->setOwner(owner);  // fake owner - but not set Owned -> for assignment stuff
				}
			}
		}
	}
	return a;
}

CScriptVarLinkPtr CTinyJS::execute_function_call(bool &execute) {
	CScriptVarLinkPtr parent = execute_literals(execute);
	CScriptVarLinkPtr a = execute_member(parent, execute);
	while (t->tk == '(') {
		if (execute) {
			if (!(*a)->isFunction()) {
				string errorMsg = "Expecting '";
				errorMsg = errorMsg + a->getName() + "' to be a function";
				throw new CScriptException(errorMsg.c_str());
			}
			t->match('('); // path += '(';

			// grab in all parameters
			vector<CScriptVarPtr> arguments;
			while(t->tk!=')') {
				CScriptVarLinkPtr value = execute_assignment(execute);
//				path += (*value)->getString();
				if (execute) {
					arguments.push_back(value);
				}
				if (t->tk!=')') { t->match(','); /*path+=',';*/ }
			}
			t->match(')'); //path+=')';
			// setup a return variable
			CScriptVarLinkPtr returnVar;
			if(execute) {
				if (!parent)
					parent = findInScopes("this");
				// if no parent use the root-scope
				CScriptVarPtr This(parent ? parent->getVarPtr() : (CScriptVarPtr )root);
				a = callFunction(execute, a, arguments, This);
			}
		} else {
			// function, but not executing - just parse args and be done
			t->match('(');
			while (t->tk != ')') {
				CScriptVarLinkPtr value = execute_base(execute);
				//	if (t->tk!=')') t->match(',');
			}
			t->match(')');
		}
		a = execute_member(a, execute);
	}
	return a;
}
// R->L: Precedence 3 (in-/decrement) ++ --
// R<-L: Precedence 4 (unary) ! ~ + - typeof void delete 
CScriptVarLinkPtr CTinyJS::execute_unary(bool &execute) {
	CScriptVarLinkPtr a;
	if (t->tk=='-') {
		t->match('-');
		a = execute_unary(execute);
		if (execute) {
			CheckRightHandVar(execute, a);
			CScriptVarPtr zero = newScriptVar(0);
			a = mathsOp(execute, zero, a, '-');
		}
	} else if (t->tk=='+') {
		t->match('+');
		a = execute_unary(execute);
		CheckRightHandVar(execute, a);
	} else if (t->tk=='!') {
		t->match('!'); // binary not
		a = execute_unary(execute);
		if (execute) {
			CheckRightHandVar(execute, a);
			a = mathsOp(execute, a, constZero, LEX_EQUAL);
		}
	} else if (t->tk=='~') {
		t->match('~'); // binary neg
		a = execute_unary(execute);
		if (execute) {
			CheckRightHandVar(execute, a);
			a = mathsOp(execute, a, constZero, '~');
		}
	} else if (t->tk==LEX_R_TYPEOF) {
		t->match(LEX_R_TYPEOF); // void
		a = execute_unary(execute);
		if (execute) {
			CheckRightHandVar(execute, a);
			a(newScriptVar((*a)->getVarType()));
		}
	} else if (t->tk==LEX_R_VOID) {
		t->match(LEX_R_VOID); // void
		a = execute_unary(execute);
		if (execute) {
			CheckRightHandVar(execute, a);
			a(constScriptVar(Undefined));
		}
	} else if (t->tk==LEX_R_DELETE) {
		t->match(LEX_R_DELETE); // delete
		a = execute_unary(execute);
		if (execute) {
			// !!! no right-hand-check by delete
			if(a->isOwned() && a->isDeletable()) {
				a->getOwner()->removeLink(a);	// removes the link from owner
				a(constScriptVar(true));
			}
			else
				a(constScriptVar(false));
		}
	} else if (t->tk==LEX_PLUSPLUS || t->tk==LEX_MINUSMINUS) {
		int op = t->tk;
		t->match(t->tk); // pre increment/decrement
		a = execute_function_call(execute);
		if (execute) {
			if(a->isOwned() && a->isWritable())
			{
				CScriptVarPtr res = mathsOp(execute, a, constOne, op==LEX_PLUSPLUS ? '+' : '-');
				// in-place add/subtract
				a->replaceWith(res);
				a = res;
			}
		}
	} else {
		a = execute_function_call(execute);
	}
	// post increment/decrement
	if (t->tk==LEX_PLUSPLUS || t->tk==LEX_MINUSMINUS) {
		int op = t->tk;
		t->match(t->tk);
		if (execute) {
			if(a->isOwned() && a->isWritable())
			{
//				TRACE("post-increment of %s and a is %sthe owner\n", a->name.c_str(), a->owned?"":"not ");
				CScriptVarPtr res = a;
				CScriptVarPtr new_a = mathsOp(execute, a, constOne, op==LEX_PLUSPLUS ? '+' : '-');
				a->replaceWith(new_a);
				a = res;
			}
		}
	}
	return a;
}

// L->R: Precedence 5 (term) * / %
CScriptVarLinkPtr CTinyJS::execute_term(bool &execute) {
	CScriptVarLinkPtr a = execute_unary(execute);
	if (t->tk=='*' || t->tk=='/' || t->tk=='%') {
		CheckRightHandVar(execute, a);
		while (t->tk=='*' || t->tk=='/' || t->tk=='%') {
			int op = t->tk;
			t->match(t->tk);
			CScriptVarLinkPtr b = execute_unary(execute); // L->R
			if (execute) {
				CheckRightHandVar(execute, b);
				a = mathsOp(execute, a, b, op);
			}
		}
	}
	return a;
}

// L->R: Precedence 6 (addition/subtraction) + -
CScriptVarLinkPtr CTinyJS::execute_expression(bool &execute) {
	CScriptVarLinkPtr a = execute_term(execute);
	if (t->tk=='+' || t->tk=='-') {
		CheckRightHandVar(execute, a);
		while (t->tk=='+' || t->tk=='-') {
			int op = t->tk;
			t->match(t->tk);
			CScriptVarLinkPtr b = execute_term(execute); // L->R
			if (execute) {
				CheckRightHandVar(execute, b);
				 // not in-place, so just replace
				 a = mathsOp(execute, a, b, op);
			}
		}
	}
	return a;
}

// L->R: Precedence 7 (bitwise shift) << >> >>>
CScriptVarLinkPtr CTinyJS::execute_binary_shift(bool &execute) {
	CScriptVarLinkPtr a = execute_expression(execute);
	if (t->tk==LEX_LSHIFT || t->tk==LEX_RSHIFT || t->tk==LEX_RSHIFTU) {
		CheckRightHandVar(execute, a);
		while (t->tk>=LEX_SHIFTS_BEGIN && t->tk<=LEX_SHIFTS_END) {
			int op = t->tk;
			t->match(t->tk);

			CScriptVarLinkPtr b = execute_expression(execute); // L->R
			if (execute) {
				CheckRightHandVar(execute, a);
				 // not in-place, so just replace
				 a = mathsOp(execute, a, b, op);
			}
		}
	}
	return a;
}
// L->R: Precedence 8 (relational) < <= > <= in instanceof
// L->R: Precedence 9 (equality) == != === !===
CScriptVarLinkPtr CTinyJS::execute_relation(bool &execute, int set, int set_n) {
	CScriptVarLinkPtr a = set_n ? execute_relation(execute, set_n, 0) : execute_binary_shift(execute);
	if ((set==LEX_EQUAL && t->tk>=LEX_RELATIONS_1_BEGIN && t->tk<=LEX_RELATIONS_1_END)
				||	(set=='<' && (t->tk==LEX_LEQUAL || t->tk==LEX_GEQUAL || t->tk=='<' || t->tk=='>' || t->tk == LEX_R_IN || t->tk == LEX_R_INSTANCEOF))) {
		CheckRightHandVar(execute, a);
		while ((set==LEX_EQUAL && t->tk>=LEX_RELATIONS_1_BEGIN && t->tk<=LEX_RELATIONS_1_END)
					||	(set=='<' && (t->tk==LEX_LEQUAL || t->tk==LEX_GEQUAL || t->tk=='<' || t->tk=='>' || t->tk == LEX_R_IN || t->tk == LEX_R_INSTANCEOF))) {
			int op = t->tk;
			t->match(t->tk);
			CScriptVarLinkPtr b = set_n ? execute_relation(execute, set_n, 0) : execute_binary_shift(execute); // L->R
			if (execute) {
				CheckRightHandVar(execute, b);
				if(op == LEX_R_IN) {
					a(constScriptVar( (*b)->findChildWithPrototypeChain((*a)->getString())!= 0 ));
				} else if(op == LEX_R_INSTANCEOF) {
					CScriptVarLink *prototype = (*b)->findChild(TINYJS_PROTOTYPE_CLASS);
					if(!prototype)
						throwError(execute, "invalid 'instanceof' operand b");
					else {
						unsigned int uniqueID = getUniqueID();
						CScriptVarPtr object = (*a)->findChild(TINYJS___PROTO___VAR);
						while( object && object!=prototype->getVarPtr() && object->getTempraryID() != uniqueID) {
							object->setTemporaryID(uniqueID); // prevents recursions
							object = object->findChild(TINYJS___PROTO___VAR);
						}
						a(constScriptVar(object && object==prototype->getVarPtr()));
					}
				} else
					a = mathsOp(execute, a, b, op);
			}
		}
	}
	return a;
}

// L->R: Precedence 10 (bitwise-and) &
// L->R: Precedence 11 (bitwise-xor) ^
// L->R: Precedence 12 (bitwise-or) |
CScriptVarLinkPtr CTinyJS::execute_binary_logic(bool &execute, int op, int op_n1, int op_n2) {
	CScriptVarLinkPtr a = op_n1 ? execute_binary_logic(execute, op_n1, op_n2, 0) : execute_relation(execute);
	if (t->tk==op) {
		CheckRightHandVar(execute, a);
		while (t->tk==op) {
			t->match(t->tk);
			CScriptVarLinkPtr b = op_n1 ? execute_binary_logic(execute, op_n1, op_n2, 0) : execute_relation(execute); // L->R
			if (execute) {
				CheckRightHandVar(execute, b);
				a = mathsOp(execute, a, b, op);
			}
		}
	}
	return a;
}
// L->R: Precedence 13 ==> (logical-or) &&
// L->R: Precedence 14 ==> (logical-or) ||
CScriptVarLinkPtr CTinyJS::execute_logic(bool &execute, int op /*= LEX_OROR*/, int op_n /*= LEX_ANDAND*/) {
	CScriptVarLinkPtr a = op_n ? execute_logic(execute, op_n, 0) : execute_binary_logic(execute);
	if (t->tk==op) {
		if(execute) {
			CheckRightHandVar(execute, a);
			CScriptVarPtr _a = a;
			CScriptVarLinkPtr b=a;
			bool _a_bool = _a->getBool();
			bool shortCircuit = false;
			while (t->tk==op) {
				int binary_op = t->tk;
				t->match(t->tk);
				if (op==LEX_ANDAND) {
					binary_op = '&';
					shortCircuit = !_a_bool;
				} else {
					binary_op = '|';
					shortCircuit = _a_bool;
				}
				b = op_n ? execute_logic(shortCircuit ? noexecute : execute, op_n, 0) : execute_binary_logic(shortCircuit ? noexecute : execute); // L->R
				CheckRightHandVar(execute, b);
				if (execute && !shortCircuit) {
					CheckRightHandVar(execute, b);
					_a = mathsOp(execute, constScriptVar(_a_bool), constScriptVar((*b)->getBool()), binary_op);
					_a_bool = _a->getBool();
				}
			}
			if (_a_bool && ( (op==LEX_ANDAND && !shortCircuit) || (op==LEX_OROR) ) )
				return b;
			else 
				return constFalse;
		} else
			op_n ? execute_logic(execute, op_n, 0) : execute_binary_logic(execute); // L->R
	}
	return a;
}

// L<-R: Precedence 15 (condition) ?: 
CScriptVarLinkPtr CTinyJS::execute_condition(bool &execute)
{
	CScriptVarLinkPtr a = execute_logic(execute);
	if (t->tk=='?')
	{
		CheckRightHandVar(execute, a);
		t->match(t->tk);
		bool cond = execute && (*a)->getBool();
		CScriptVarLinkPtr b;
		a = execute_condition(cond ? execute : noexecute ); // L<-R
//		CheckRightHandVar(execute, a);
		t->match(':');
		b = execute_condition(cond ? noexecute : execute); // R-L
//		CheckRightHandVar(execute, b);
		if(!cond)
			return b;
	}
	return a;
}
	
// L<-R: Precedence 16 (assignment) = += -= *= /= %= <<= >>= >>>= &= |= ^=
CScriptVarLinkPtr CTinyJS::execute_assignment(bool &execute) {
	CScriptVarLinkPtr lhs = execute_condition(execute);
	if (t->tk=='=' || (t->tk>=LEX_ASSIGNMENTS_BEGIN && t->tk<=LEX_ASSIGNMENTS_END) ) {
		int op = t->tk;
		CScriptTokenizer::ScriptTokenPosition leftHandPos = t->getPos();
		t->match(t->tk);
		CScriptVarLinkPtr rhs = execute_assignment(execute); // L<-R
		if (execute) {
			bool lhs_is_accessor = lhs.getLink() != lhs.getRealLink() && lhs.getRealLink() && (*lhs.getRealLink())->isAccessor();
			if (!lhs->isOwned() && lhs->getName().empty() && !lhs_is_accessor) {
				throw new CScriptException("invalid assignment left-hand side", t->currentFile, leftHandPos.currentLine(), leftHandPos.currentColumn());
			} else if (op != '=' && !lhs->isOwned() && !lhs_is_accessor) {
				throwError(execute, lhs->getName() + " is not defined");
			}
			else if(lhs->isWritable()) {
				if (op=='=') {
					if (!lhs->isOwned() && !lhs_is_accessor) {
						CScriptVarLink *realLhs;
						if(lhs->isOwner())
							realLhs = lhs->getOwner()->addChildNoDup(lhs->getName(), lhs);
						else
							realLhs = root->addChildNoDup(lhs->getName(), lhs);
						lhs = realLhs;
					}
					lhs.replaceVar(execute, rhs);
					return rhs->getVarPtr();
				} else {
					CScriptVarPtr result;
					if (op==LEX_PLUSEQUAL)
						result = mathsOp(execute, lhs, rhs, '+');
					else if (op==LEX_MINUSEQUAL)
						result = mathsOp(execute, lhs, rhs, '-');
					else if (op==LEX_ASTERISKEQUAL)
						result = mathsOp(execute, lhs, rhs, '*');
					else if (op==LEX_SLASHEQUAL)
						result = mathsOp(execute, lhs, rhs, '/');
					else if (op==LEX_PERCENTEQUAL)
						result = mathsOp(execute, lhs, rhs, '%');
					else if (op==LEX_LSHIFTEQUAL)
						result = mathsOp(execute, lhs, rhs, LEX_LSHIFT);
					else if (op==LEX_RSHIFTEQUAL)
						result = mathsOp(execute, lhs, rhs, LEX_RSHIFT);
					else if (op==LEX_RSHIFTUEQUAL)
						result = mathsOp(execute, lhs, rhs, LEX_RSHIFTU);
					else if (op==LEX_ANDEQUAL)
						result = mathsOp(execute, lhs, rhs, '&');
					else if (op==LEX_OREQUAL)
						result = mathsOp(execute, lhs, rhs, '|');
					else if (op==LEX_XOREQUAL)
						result = mathsOp(execute, lhs, rhs, '^');
					
					lhs->replaceWith(result);
					return result;
				}
			}
		}
	}
	else 
		CheckRightHandVar(execute, lhs);
	return lhs;
}
// L->R: Precedence 17 (comma) ,
CScriptVarLinkPtr CTinyJS::execute_base(bool &execute) {
	CScriptVarLinkPtr a;
	for(;;)
	{
		a = execute_assignment(execute); // L->R
		if (t->tk == ',') {
			t->match(',');
		} else
			break;
	}
	return a;
}
void CTinyJS::execute_block(bool &execute) {
	if(execute) {
		t->match('{');
		CScopeControl ScopeControl(this);
		if(scope()->scopeLet() != scope()) // add a LetScope only if needed
			ScopeControl.addLetScope();
		while (t->tk && t->tk!='}')
			execute_statement(execute);
		t->match('}');
		// scopes.pop_back();
	}
	else 
		t->skip(t->getToken().Int());
}
CScriptVarLinkPtr CTinyJS::execute_statement(bool &execute) {
	CScriptVarLinkPtr ret;
	if (t->tk=='{') {
		/* A block of code */
		execute_block(execute);
	} else if (t->tk==';') {
		/* Empty statement - to allow things like ;;; */
		t->match(';');
	} else if (t->tk==LEX_ID) {
		ret = execute_base(execute);
		t->match(';');
	} else if (t->tk==LEX_R_VAR || t->tk==LEX_R_LET) {
		if(execute)
		{
			bool let = t->tk==LEX_R_LET, let_ext=false;
			t->match(t->tk);
			CScopeControl ScopeControl(this);
			if(let && t->tk=='(') {
				let_ext = true;
				t->match(t->tk);
				ScopeControl.addLetScope();
			}
			CScriptVarPtr in_scope = let ? scope()->scopeLet() : scope()->scopeVar();
			for(;;) {
				CScriptVarLinkPtr a;
				string var = t->tkStr();
				t->match(LEX_ID);
				a = in_scope->findChildOrCreate(var);
				a->setDeletable(false);
				// sort out initialiser
				if (t->tk == '=') {
					t->match('=');
					a.replaceVar(execute, execute_assignment(execute));
				}
				if (t->tk == ',') 
					t->match(',');
				else
					break;
			}
			if(let_ext) {
				t->match(')');
				execute_statement(execute);
			} else
				t->match(';');
		} else
			t->skip(t->getToken().Int());
	} else if (t->tk==LEX_R_WITH) {
		if(execute) {
			t->match(LEX_R_WITH);
			t->match('(');
			CScriptVarLinkPtr var = execute_base(execute);
			t->match(')');
			CScopeControl ScopeControl(this);
			ScopeControl.addWithScope(var);
			ret = execute_statement(execute);
		} else
			t->skip(t->getToken().Int());
	} else if (t->tk==LEX_R_IF) {
		if(execute) {
			t->match(LEX_R_IF);
			t->match('(');
			bool cond = (*execute_base(execute))->getBool();
			t->match(')');
			if(cond && execute) {
				t->match(LEX_T_SKIP);
				execute_statement(execute);
			} else {
				t->check(LEX_T_SKIP);
				t->skip(t->getToken().Int());
			}
			if (t->tk==LEX_R_ELSE) {
				if(!cond && execute) {
					t->match(LEX_R_ELSE);
					execute_statement(execute);
				}
				else
					t->skip(t->getToken().Int());
			}
		} else
			t->skip(t->getToken().Int());
	} else if (t->tk==LEX_R_DO) {
		if(execute) {
			t->match(LEX_R_DO);
			CScriptTokenizer::ScriptTokenPosition loopStart = t->getPos();
			int old_loop_runtimeFlags = runtimeFlags & RUNTIME_LOOP_MASK;
			runtimeFlags = (runtimeFlags & ~RUNTIME_LOOP_MASK) | RUNTIME_CANBREAK | RUNTIME_CANCONTINUE;
			bool loopCond = true;
			while (loopCond && execute) {
				t->setPos(loopStart);
				execute_statement(execute);
				if(!execute)
				{
					// break or continue
					if(runtimeFlags & (RUNTIME_BREAK | RUNTIME_CONTINUE))
					{
						execute = true;
						bool Break = (runtimeFlags & RUNTIME_BREAK) != 0;
						runtimeFlags &= ~(RUNTIME_BREAK | RUNTIME_CONTINUE);
						if(Break) {
							t->match(LEX_R_WHILE);
							t->check('(');
							t->skip(t->getToken().Int());
							t->match(';');
							break;
						}
					}
					// other stuff e.g return, throw
				}
				t->match(LEX_R_WHILE);
				if(execute) {
					t->match('(');
					loopCond = (*execute_base(execute))->getBool();
					t->match(')');
				} else {
					t->check('(');
					t->skip(t->getToken().Int());
				}
				t->match(';');
			}
			runtimeFlags = (runtimeFlags & ~RUNTIME_LOOP_MASK) | old_loop_runtimeFlags;
		} else 
			t->skip(t->getToken().Int());
	} else if (t->tk==LEX_R_WHILE) {
		if(execute) {
			t->match(LEX_R_WHILE);
			bool loopCond;
			t->match('(');
			CScriptTokenizer::ScriptTokenPosition condStart = t->getPos();
			loopCond = (*execute_base(execute))->getBool();
			t->match(')');
			if(loopCond && execute) {
				t->match(LEX_T_SKIP);
				CScriptTokenizer::ScriptTokenPosition loopStart = t->getPos();
				CScriptTokenizer::ScriptTokenPosition loopEnd = loopStart;
				int old_loop_runtimeFlags = runtimeFlags & RUNTIME_LOOP_MASK;
				runtimeFlags = (runtimeFlags & ~RUNTIME_LOOP_MASK) | RUNTIME_CANBREAK | RUNTIME_CANCONTINUE;
				while (loopCond && execute) {
					t->setPos(loopStart);
					execute_statement(execute);
					if(loopEnd == loopStart) // first loop-pass
						loopEnd = t->getPos();
					if(!execute) {
						// break or continue
						if(runtimeFlags & (RUNTIME_BREAK | RUNTIME_CONTINUE))
						{
							execute = true;
							bool Break = (runtimeFlags & RUNTIME_BREAK) != 0;
							runtimeFlags &= ~(RUNTIME_BREAK | RUNTIME_CONTINUE);
							if(Break) break;
						}
						// other stuff e.g return, throw
					}
					if(execute) {
						t->setPos(condStart);
						loopCond = (*execute_base(execute))->getBool();
					}
				}
				t->setPos(loopEnd);
				runtimeFlags = (runtimeFlags & ~RUNTIME_LOOP_MASK) | old_loop_runtimeFlags;
			} else {
				t->check(LEX_T_SKIP);
				t->skip(t->getToken().Int());
			}
		} else
			t->skip(t->getToken().Int());
	} else if (t->tk==LEX_T_FOR_IN || t->tk == LEX_T_FOR_EACH_IN) {
		if(execute) {
			bool for_each = t->tk == LEX_T_FOR_EACH_IN;
			t->match(t->tk);
			t->match('(');
			CScriptVarLinkPtr for_var;
			CScriptVarLinkPtr for_in_var;

			CScopeControl ScopeControl(this);
			ScopeControl.addLetScope();

			if(t->tk == LEX_R_LET) {
				t->match(LEX_R_LET);
				string var = t->tkStr();
				t->match(LEX_ID);
				for_var = scope()->scopeLet()->findChildOrCreate(var);
			}
			else
				for_var = execute_function_call(execute);

			t->match(LEX_R_IN);

			for_in_var = execute_function_call(execute);
			CheckRightHandVar(execute, for_in_var);
			t->match(')');
			if( (*for_in_var)->Childs.size() ) {
				if(!for_var->isOwned()) {
					CScriptVarLink *real_for_var;
					if(for_var->isOwner())
						real_for_var = for_var->getOwner()->addChildNoDup(for_var->getName(), for_var);
					else
						real_for_var = root->addChildNoDup(for_var->getName(), for_var);
					for_var = real_for_var;
				}

				CScriptTokenizer::ScriptTokenPosition loopStart = t->getPos();

				int old_loop_runtimeFlags = runtimeFlags & RUNTIME_LOOP_MASK;
				runtimeFlags = (runtimeFlags & ~RUNTIME_LOOP_MASK) | RUNTIME_CANBREAK | RUNTIME_CANCONTINUE;
				for(SCRIPTVAR_CHILDS_it it = (*for_in_var)->Childs.begin(); execute && it != (*for_in_var)->Childs.end(); ++it) {
					CScriptVarLink *link = for_var.getLink();
					if(link) {
						if (for_each)
							link->replaceWith(*it);
						else
							link->replaceWith(newScriptVar((*it)->getName()));
					} 					else ASSERT(0);
					t->setPos(loopStart);
					execute_statement(execute);
					if(!execute)
					{
						// break or continue
						if(runtimeFlags & (RUNTIME_BREAK | RUNTIME_CONTINUE))
						{
							execute = true;
							bool Break = (runtimeFlags & RUNTIME_BREAK)!=0;
							runtimeFlags &= ~(RUNTIME_BREAK | RUNTIME_CONTINUE);
							if(Break) break;
						}
						// other stuff e.g return, throw
					}
				}
				runtimeFlags = (runtimeFlags & ~RUNTIME_LOOP_MASK) | old_loop_runtimeFlags;
			} else {
				execute_statement(noexecute);
			}
		} else {
			t->skip(t->getToken().Int());
			execute_statement(execute);
		}
	} else if (t->tk==LEX_R_FOR) {
		if(execute)
		{
			t->match(LEX_R_FOR);
			t->match('(');
			CScopeControl ScopeControl(this);
			if(t->tk == LEX_R_LET) 
				ScopeControl.addLetScope();
			execute_statement(execute); // initialisation
			CScriptTokenizer::ScriptTokenPosition conditionStart = t->getPos();
			bool cond_empty = true;
			bool loopCond = execute;	// Empty Condition -->always true
			if(t->tk != ';') {
				cond_empty = false;
				loopCond = execute && (*execute_base(execute))->getBool();
			}
			t->match(';');
			CScriptTokenizer::ScriptTokenPosition iterStart = t->getPos();
			bool iter_empty = true;
			if(t->tk != ')')
			{
				iter_empty = false;
				execute_base(noexecute); // iterator
			}
			t->match(')');
			if(loopCond) { // when execute==false then loopCond always false to
				CScriptTokenizer::ScriptTokenPosition loopStart = t->getPos();
				CScriptTokenizer::ScriptTokenPosition loopEnd = t->getPos();

				int old_loop_runtimeFlags = runtimeFlags & RUNTIME_LOOP_MASK;
				runtimeFlags = (runtimeFlags & ~RUNTIME_LOOP_MASK) | RUNTIME_CANBREAK | RUNTIME_CANCONTINUE;
				while (loopCond && execute) {
					t->setPos(loopStart);
					execute_statement(execute);
					if(loopEnd == loopStart) // first loop-pass
						loopEnd = t->getPos();
					if(!execute) {
						// break or continue
						if(runtimeFlags & (RUNTIME_BREAK | RUNTIME_CONTINUE)) {
							execute = true;
							bool Break = (runtimeFlags & RUNTIME_BREAK)!=0;
							runtimeFlags &= ~(RUNTIME_BREAK | RUNTIME_CONTINUE);
							if(Break) break;
						}
						// other stuff e.g return, throw
					} 
					if(execute) {
						if(!iter_empty) {
							t->setPos(iterStart);;
							execute_base(execute);
						}
						if(!cond_empty) {
							t->setPos(conditionStart);
							loopCond = (*execute_base(execute))->getBool();
						}
					}
				}
				t->setPos(loopEnd);
				runtimeFlags = (runtimeFlags & ~RUNTIME_LOOP_MASK) | old_loop_runtimeFlags;
			} else {
				execute_statement(noexecute);
			}
		}  else
			t->skip(t->getToken().Int());
	} else if (t->tk==LEX_R_BREAK) {
		t->match(LEX_R_BREAK);
		if(execute) {
			if(runtimeFlags & RUNTIME_CANBREAK)
			{
				runtimeFlags |= RUNTIME_BREAK;
				execute = false;
			}
			else
				throw new CScriptException("'break' must be inside loop or switch", t->currentFile, t->currentLine(), t->currentColumn());
		}
		t->match(';');
	} else if (t->tk==LEX_R_CONTINUE) {
		t->match(LEX_R_CONTINUE);
		if(execute) {
			if(runtimeFlags & RUNTIME_CANCONTINUE)
			{
				runtimeFlags |= RUNTIME_CONTINUE;
				execute = false;
			}
			else
				throw new CScriptException("'continue' must be inside loop or switch", t->currentFile, t->currentLine(), t->currentColumn());
		}
		t->match(';');
	} else if (t->tk==LEX_R_RETURN) {
		if (execute) {
			if (IS_RUNTIME_CANRETURN) {
				t->match(LEX_R_RETURN);
				CScriptVarPtr result;
				if (t->tk != ';')
					result = execute_base(execute);
				if(result) scope()->scopeVar()->addChildNoDup(TINYJS_RETURN_VAR, result);
			} else 
				throw new CScriptException("'return' statement, but not in a function.", t->currentFile, t->currentLine(), t->currentColumn());
			execute = false;
		}
		else {
			t->match(LEX_R_RETURN);
			if (t->tk != ';')
				execute_base(execute);
		}
		t->match(';');
	} else if (t->tk == LEX_T_FUNCTION_OPERATOR) {
		// ignore force anonymous at statement-level
		ASSERT(0); // no functions-operator statement-level
		t->match(t->tk);
	} else if (t->tk == LEX_R_FUNCTION) {
		if(execute) {
			CScriptTokenDataFnc &Fnc = t->getToken().Fnc();
			if(!Fnc.name.length())
				throw new CScriptException("Functions defined at statement-level are meant to have a name.");
			else {
				CScriptVarLinkPtr funcVar = parseFunctionDefinition(t->getToken());
				scope()->scopeVar()->addChildNoDup(funcVar->getName(), funcVar)->setDeletable(false);
			}
		}
		t->match(t->tk);
	} else if (t->tk==LEX_R_TRY) {
		if(execute) {
			t->match(LEX_R_TRY);
			// save runtimeFlags
			int old_throw_runtimeFlags = runtimeFlags & RUNTIME_THROW_MASK;
			// set runtimeFlags
			runtimeFlags = runtimeFlags | RUNTIME_CANTHROW;

			execute_block(execute);
			CScriptVarPtr exceptionVar = this->exceptionVar; // remember exceptionVar
			this->exceptionVar = CScriptVarPtr(); // clear exeptionVar;

			bool isThrow = (runtimeFlags & RUNTIME_THROW) != 0;
			// restore runtimeFlags
			runtimeFlags = (runtimeFlags & ~RUNTIME_THROW_MASK) | old_throw_runtimeFlags;

			if(isThrow) execute = true;
			while(t->tk == LEX_R_CATCH) // expect catch, finally
			{
				if(execute && isThrow) { // when a catch-block is found or no throw thens skip all followed
					t->match(LEX_R_CATCH);
					t->match('(');
					string exception_var_name = t->tkStr();
					t->match(LEX_ID);
					CScopeControl ScopeControl(this);
					ScopeControl.addLetScope();
					scope()->scopeLet()->addChild(exception_var_name, exceptionVar);
					bool condition = true;
					if(t->tk == LEX_R_IF) {
						t->match(LEX_R_IF);
						condition = (*execute_base(execute))->getPrimitivVar(execute)->getBool();
					}
					t->match(')');
					if(execute && condition) {
						isThrow = false;
						execute_block(execute);
					} else
						execute_block(noexecute);
				} else
					t->skip(t->getToken().Int());
			}
			if(t->tk == LEX_R_FINALLY) {
				t->match(LEX_R_FINALLY);
				bool finally_execute = true; // alway execute finammy-block
				execute_block(finally_execute);
			}
			if(isThrow && (runtimeFlags & RUNTIME_THROW)==0) { // no catch-block found and no new exception
				if(runtimeFlags & RUNTIME_CANTHROW) {				// throw again
					runtimeFlags |= RUNTIME_THROW;
					execute = false;
					this->exceptionVar = exceptionVar;
				} else
					throw new CScriptException("uncaught exception: ", t->currentFile, t->currentLine(), t->currentColumn());
			}
		} else
			t->skip(t->getToken().Int());
	} else if (t->tk==LEX_R_THROW) {
		CScriptTokenizer::ScriptTokenPosition tokenPos = t->getPos();
//		int tokenStart = t->getToken().pos;
		t->match(LEX_R_THROW);
		CScriptVarPtr a = execute_base(execute);
		if(execute) {
			if(runtimeFlags & RUNTIME_CANTHROW) {
				runtimeFlags |= RUNTIME_THROW;
				execute = false;
				exceptionVar = a;
			}
			else
				throw new CScriptException("uncaught exception: '"+a->getString()+"'", t->currentFile, tokenPos.currentLine(), tokenPos.currentColumn());
		}
	} else if (t->tk == LEX_R_SWITCH) {
		if(execute) {
			t->match(LEX_R_SWITCH);
			t->match('(');
			CScriptVarPtr SwitchValue = execute_base(execute);
			t->match(')');
			if(execute) {
				// save runtimeFlags
				int old_switch_runtimeFlags = runtimeFlags & RUNTIME_BREAK_MASK;
				// set runtimeFlags
				runtimeFlags = (runtimeFlags & ~RUNTIME_BREAK_MASK) | RUNTIME_CANBREAK;
				bool found = false ,execute = false;
				t->match('{');
				CScopeControl ScopeControl(this);
				ScopeControl.addLetScope();
				if(t->tk == LEX_R_CASE || t->tk == LEX_R_DEFAULT || t->tk == '}') {
					CScriptTokenizer::ScriptTokenPosition defaultStart = t->getPos();
					bool hasDefault = false;
					while (t->tk) {
						if(t->tk == LEX_R_CASE) {
							if(found) {
								t->skip(t->getToken().Int());
								if(execute)  t->match(':');
								else t->skip(t->getToken().Int());
							} else {
								t->match(LEX_R_CASE);
								execute = true;
								CScriptVarLinkPtr CaseValue = execute_base(execute);
								if(execute) {
									CaseValue = mathsOp(execute, CaseValue, SwitchValue, LEX_EQUAL);
									found = execute = (*CaseValue)->getBool();
									if(found) t->match(':');
									else t->skip(t->getToken().Int());
								} else {
									found = true;
									t->skip(t->getToken().Int());
								}
							}
						} else if(t->tk == LEX_R_DEFAULT) {
							t->match(LEX_R_DEFAULT);
							if(found) {
								if(execute)  t->match(':');
								else t->skip(t->getToken().Int());
							} else {
								hasDefault = true;
								defaultStart = t->getPos();
								t->skip(t->getToken().Int());
							}
						} else if(t->tk == '}') {
							if(!found && hasDefault) {
								found = execute = true;
								t->setPos(defaultStart);
								t->match(':');
							} else
								break;
						} else
							execute_statement(execute);
					}
					t->match('}');
					if(!found || (runtimeFlags & RUNTIME_BREAK) )
						execute = true;
					// restore runtimeFlags
					runtimeFlags = (runtimeFlags & ~RUNTIME_BREAK_MASK) | old_switch_runtimeFlags;
				} else
					throw new CScriptException("invalid switch statement");
			} else
				execute_block(execute);
		} else
			t->skip(t->getToken().Int());
	} else if(t->tk == LEX_T_LABEL) {
		t->match(LEX_T_LABEL); // ignore Labels
		t->match(':');
	} else if(t->tk != LEX_EOF) {
		/* Execute a simple statement that only contains basic arithmetic... */
		ret = execute_base(execute);
		t->match(';');
	} else
		t->match(LEX_EOF);
	return ret;
}


/// Finds a child, looking recursively up the scopes
CScriptVarLink *CTinyJS::findInScopes(const string &childName) {
	return scope()->findInScopes(childName);
/*
	CScriptVarLink *v = scopes.back()->findChild(childName);
	if(!v && scopes.front() != scopes.back())
		v = scopes.front()->findChild(childName);
	return v;
*/
}

/// Get all Keynames of en given object (optionial look up the prototype-chain)
void CTinyJS::keys(STRING_VECTOR_t &Keys, CScriptVarPtr object, bool WithPrototypeChain) {
	CScriptVarLink *__proto__;
	for(SCRIPTVAR_CHILDS_it it = object->Childs.begin(); it != object->Childs.end(); ++it) {
		if((*it)->isEnumerable())
			Keys.push_back((*it)->getName());

		if (WithPrototypeChain) {
			if( (__proto__ = (*(*it))->findChild(TINYJS___PROTO___VAR)) ) 
				keys(Keys, __proto__, WithPrototypeChain);
			else if((*(*it))->isString())
				keys(Keys, stringPrototype, WithPrototypeChain);
			else if((*(*it))->isArray())
				keys(Keys, arrayPrototype, WithPrototypeChain);
			else if((*(*it))->isFunction())
				keys(Keys, functionPrototype, WithPrototypeChain);
			else
				keys(Keys, objectPrototype, false);
		}
	}
}
#if 0
/// Look up in any parent classes of the given object
CScriptVarLink *CTinyJS::findInPrototypeChain(CScriptVarPtr object, const string &name) {
	unsigned int uniqueID = getUniqueID();
	// Look for links to actual parent classes
	CScriptVarLink *__proto__;
//	CScriptVar *_object = object;
	while( (__proto__ = object->findChild(TINYJS___PROTO___VAR)) ) {
			CScriptVarLink *implementation = (*__proto__)->findChild(name);
			if (implementation){
				return implementation;
			}
			object = __proto__.getVarPtr();
	}
/*
	if (object->isString()) {
		CScriptVarLink *implementation = stringPrototype->findChild(name);
		if (implementation) return implementation;
	}
	if (object->isArray()) {
		CScriptVarLink *implementation = arrayPrototype->findChild(name);
		if (implementation) return implementation;
	}
	if (object->isFunction()) {
		CScriptVarLink *implementation = functionPrototype->findChild(name);
		if (implementation) return implementation;
	}
	CScriptVarLink *implementation = objectPrototype->findChild(name);
	if (implementation) return implementation;
*/
	return 0;
}
#endif
//////////////////////////////////////////////////////////////////////////
/// Object
//////////////////////////////////////////////////////////////////////////

void CTinyJS::native_Object(const CFunctionsScopePtr &c, void *data) {
	CScriptVarPtr objc = c->getArgument(0);
	if(objc->isUndefined() || objc->isNull())
		c->setReturnVar(newScriptVar(Object));
	else if(objc->isPrimitive()) {
		c->setReturnVar(newScriptVar(ObjectWrap, objc)); 
	} else
		c->setReturnVar(objc);
}
void CTinyJS::native_Object_hasOwnProperty(const CFunctionsScopePtr &c, void *data) {
	c->setReturnVar(c->constScriptVar(c->getArgument("this")->findChild(c->getArgument("prop")->getString()) != 0));
}
void CTinyJS::native_Object_getPrototypeOf(const CFunctionsScopePtr &c, void *data) {
	if(c->getArgumentsLength()>=1) {
		c->setReturnVar(c->getArgument(0)->findChild(TINYJS___PROTO___VAR));
	}
	// TODO throw error 
}
void CTinyJS::native_Object_valueOf(const CFunctionsScopePtr &c, void *data) {
	bool execute = true;
	c->setReturnVar(c->getArgument("this")->_valueOf(execute));
}
void CTinyJS::native_Object_toString(const CFunctionsScopePtr &c, void *data) {
	bool execute = true;
	int radix = 10;
	if(c->getArgumentsLength()>=1) radix = c->getArgument("radix")->getInt();
	c->setReturnVar(c->getArgument("this")->_toString(execute, radix));
}

//////////////////////////////////////////////////////////////////////////
/// Array
//////////////////////////////////////////////////////////////////////////

void CTinyJS::native_Array(const CFunctionsScopePtr &c, void *data) {
//	CScriptVar *returnVar = new CScriptVarArray(c->getContext());
	CScriptVarPtr returnVar = c->newScriptVar(Array);
	c->setReturnVar(returnVar);
	int length = c->getArgumentsLength();
	if(length == 1 && c->getArgument(0)->isNumber())
		returnVar->setArrayIndex(c->getArgument(0)->getInt(), constScriptVar(Undefined));
	else for(int i=0; i<length; i++)
		returnVar->setArrayIndex(i, c->getArgument(i));
}

//////////////////////////////////////////////////////////////////////////
/// String
//////////////////////////////////////////////////////////////////////////

void CTinyJS::native_String(const CFunctionsScopePtr &c, void *data) {
	CScriptVarPtr arg;
	if(c->getArgumentsLength()==0)
		arg = newScriptVar("");
	else
		arg = newScriptVar(c->getArgument(0)->getString());
	CScriptVarLink *This = c->findChild("this");
	This->replaceWith(newScriptVar(ObjectWrap, arg));
	c->setReturnVar(arg);
}

//////////////////////////////////////////////////////////////////////////
/// Number
//////////////////////////////////////////////////////////////////////////

void CTinyJS::native_Number(const CFunctionsScopePtr &c, void *data) {
	CScriptVarPtr arg;
	if(c->getArgumentsLength()==0)
		arg = newScriptVar(0);
	else
		arg = c->getArgument(0)->getNumericVar();
	CScriptVarLink *This = c->findChild("this");
	This->replaceWith(newScriptVar(ObjectWrap, arg));
	c->setReturnVar(arg);
}

//////////////////////////////////////////////////////////////////////////
/// Boolean
//////////////////////////////////////////////////////////////////////////

void CTinyJS::native_Boolean(const CFunctionsScopePtr &c, void *data) {
	CScriptVarPtr arg;
	if(c->getArgumentsLength()==0)
		arg = constScriptVar(false);
	else
		arg = constScriptVar(c->getArgument(0)->getBool());
	CScriptVarLink *This = c->findChild("this");
	This->replaceWith(newScriptVar(ObjectWrap, arg));
	c->setReturnVar(arg);
}

////////////////////////////////////////////////////////////////////////// 
/// Function
//////////////////////////////////////////////////////////////////////////

void CTinyJS::native_Function(const CFunctionsScopePtr &c, void *data) {
	int length = c->getArgumentsLength();
	string params, body;
	if(length>=1) 
		body = c->getArgument(length-1)->getString();
	if(length>=2) {
		params = c->getArgument(0)->getString();
		for(int i=1; i<length-1; i++)
		{
			params.append(",");
			params.append(c->getArgument(i)->getString());
		}
	}
	c->setReturnVar(parseFunctionsBodyFromString(params,body));
}

void CTinyJS::native_Function_call(const CFunctionsScopePtr &c, void *data) {
	int length = c->getArgumentsLength();
	CScriptVarPtr Fnc = c->getArgument("this");
	CScriptVarPtr This = c->getArgument(0);
	vector<CScriptVarPtr> Params;
	for(int i=1; i<length; i++)
		Params.push_back(c->getArgument(i));
	bool execute = true;
	callFunction(execute, Fnc, Params, This);
}
void CTinyJS::native_Function_apply(const CFunctionsScopePtr &c, void *data) {
	CScriptVarPtr Fnc = c->getArgument("this");
	CScriptVarPtr This = c->getArgument(0);
	CScriptVarPtr Array = c->getArgument(1);
	CScriptVarLink *Length = Array->findChild("length");
	int length = Length ? (*Length)->getInt() : 0;
	vector<CScriptVarPtr> Params;
	for(int i=0; i<length; i++) {
		CScriptVarLink *value = Array->findChild(int2string(i));
		if(value) Params.push_back(value);
		else Params.push_back(constScriptVar(Undefined));
	}
	bool execute = true;
	callFunction(execute, Fnc, Params, This);
}

////////////////////////////////////////////////////////////////////////// 
/// Error
//////////////////////////////////////////////////////////////////////////

const char *ERROR_NAME[] = {"Error", "EvalError", "RangeError", "ReferenceError", "SyntaxError", "TypeError"};

CScriptVarPtr CTinyJS::newError(ERROR_TYPES type, const char *message, const char *file, int line, int column) {
	// ERROR_NAME similar to ERROR_TYPES
	CScriptVarPtr objc = newScriptVar(Object);
	CScriptVarPtr prototype = root->findChildByPath(string(ERROR_NAME[type])+".prototype");
	if(!prototype) prototype = errorPrototype;

	objc->addChildNoDup(TINYJS___PROTO___VAR, prototype);
	if(message && *message) objc->addChild("message", newScriptVar(message));
	if(file && *file) objc->addChild("message", newScriptVar(file));
	if(line>0) objc->addChild("message", newScriptVar(line));
	if(column>0) objc->addChild("message", newScriptVar(column));
	return objc;
}
CScriptVarPtr CTinyJS::newError(ERROR_TYPES type, const CFunctionsScopePtr &c) {
	int i = c->getArgumentsLength();
	string message, fileName;
	int line=-1, column=-1; 
	if(i>0) message	= c->getArgument(0);
	if(i>1) fileName	= c->getArgument(1);
	if(i>2) line		= c->getArgument(2);
	if(i>3) column		= c->getArgument(3);
	return newError(type, message.c_str(), fileName.c_str(), line, column));
}
void CTinyJS::native_Error(const CFunctionsScopePtr &c, void *data) { c->setReturnVar(newError(Error,c)); }
void CTinyJS::native_EvalError(const CFunctionsScopePtr &c, void *data) { c->setReturnVar(newError(EvalError,c)); }
void CTinyJS::native_RangeError(const CFunctionsScopePtr &c, void *data) { c->setReturnVar(newError(RangeError,c)); }
void CTinyJS::native_ReferenceError(const CFunctionsScopePtr &c, void *data){ c->setReturnVar(newError(ReferenceError,c)); }
void CTinyJS::native_SyntaxError(const CFunctionsScopePtr &c, void *data){ c->setReturnVar(newError(SyntaxError,c)); }
void CTinyJS::native_TypeError(const CFunctionsScopePtr &c, void *data){ c->setReturnVar(newError(TypeError,c)); }

////////////////////////////////////////////////////////////////////////// 
/// global functions
//////////////////////////////////////////////////////////////////////////

void CTinyJS::native_eval(const CFunctionsScopePtr &c, void *data) {
	string Code = c->getArgument("jsCode")->getString();
	CScriptVarScopePtr scEvalScope = scopes.back(); // save scope
	scopes.pop_back(); // go back to the callers scope
	CScriptVarLinkPtr returnVar;
	CScriptTokenizer *oldTokenizer = t; t=0;
	try {
		CScriptTokenizer Tokenizer(Code.c_str(), "eval");
		t = &Tokenizer;
		bool execute = true;
		do {
			returnVar = execute_statement(execute);
			while (t->tk==';') t->match(';'); // skip empty statements
		} while (t->tk!=LEX_EOF);
	} catch (CScriptException *e) {
		t = oldTokenizer;
		scopes.push_back(scEvalScope); // restore Scopes;
		throw e;
	}
	t = oldTokenizer;
	scopes.push_back(scEvalScope); // restore Scopes;
	if(returnVar)
		c->setReturnVar(returnVar->getVarPtr());

	// check of exceptions
	int exceptionState = runtimeFlags & (RUNTIME_BREAK | RUNTIME_CONTINUE);
	runtimeFlags &= ~RUNTIME_LOOP_MASK;
	if(exceptionState) throw new CScriptEvalException(exceptionState);
}

void CTinyJS::native_isNAN(const CFunctionsScopePtr &c, void *data) {
	c->setReturnVar(constScriptVar(c->getArgument("objc")->getNumericVar()->isNaN()));
}

void CTinyJS::native_isFinite(const CFunctionsScopePtr &c, void *data) {
	CScriptVarPtr objc = c->getArgument("objc")->getNumericVar();
	c->setReturnVar(constScriptVar(!(objc->isInfinity() || objc->isNaN())));
}

void CTinyJS::native_parseInt(const CFunctionsScopePtr &c, void *) {
	string str = c->getArgument("string")->getString();
	int radix = c->getArgument("radix")->getInt();
	char *endp = 0;
	int val = strtol(str.c_str(),&endp,radix!=0 ? radix : 0);
	if(endp == str.c_str())
		c->setReturnVar(c->constScriptVar(NaN));
	else
		c->setReturnVar(c->newScriptVar(val));
}

void CTinyJS::native_parseFloat(const CFunctionsScopePtr &c, void *) {
	string str = c->getArgument("string")->getString();
	char *endp = 0;
	double val = strtod(str.c_str(),&endp);
	if(endp == str.c_str())
		c->setReturnVar(c->constScriptVar(NaN));
	else
		c->setReturnVar(c->newScriptVar(val));
}



void CTinyJS::native_JSON_parse(const CFunctionsScopePtr &c, void *data) {
	string Code = "�" + c->getArgument("text")->getString();
	// "�" is a spezal-token - it's for the tokenizer and means the code begins not in Statement-level
	CScriptVarLinkPtr returnVar;
	CScriptTokenizer *oldTokenizer = t; t=0;
	try {
		CScriptTokenizer Tokenizer(Code.c_str(), "JSON.parse", 0, -1);
		t = &Tokenizer;
		bool execute = true;
		returnVar = execute_literals(execute);
		t->match(LEX_EOF);
	} catch (CScriptException *e) {
		t = oldTokenizer;
		throw e;
	}
	t = oldTokenizer;

	if(returnVar)
		c->setReturnVar(returnVar);

// check of exceptions
//		int exceptionState = runtimeFlags & (RUNTIME_BREAK | RUNTIME_CONTINUE);
//		runtimeFlags &= ~RUNTIME_LOOP_MASK;
//		if(exceptionState) throw new CScriptEvalException(exceptionState);
}

void CTinyJS::setTemporaryID_recursive(uint32_t ID) {
	for(vector<CScriptVarPtr*>::iterator it = pseudo_statics.begin(); it!=pseudo_statics.end(); ++it)
		if(**it) (**it)->setTemporaryID_recursive(ID);
	root->setTemporaryID_recursive(ID);
}

void CTinyJS::ClearLostVars(const CScriptVarPtr &extra/*=CScriptVarPtr()*/) {
	uint32_t UniqueID = getUniqueID(); 
	setTemporaryID_recursive(UniqueID);
	if(extra) extra->setTemporaryID_recursive(UniqueID);
	CScriptVar *p = first;
	while(p)
	{
		if(p->temporaryID != UniqueID)
		{
			CScriptVarPtr var = p;
			var->removeAllChildren();
			p = var->next;
		}
		else
			p = p->next;
	}
}