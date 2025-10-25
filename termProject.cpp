// assembler.cpp
// SIC/XE 2-pass assembler — fixes for absolute-symbol direct encoding (no hardcoding).
// Build: g++ -std=c++17 -O2 -o assembler assembler.cpp
// Run: ./assembler <source.asm>

#include <bits/stdc++.h>
using namespace std;

// ---------- utilities ----------
static inline string trim(const string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
static inline vector<string> split_ws(const string &s) {
    vector<string> out; istringstream iss(s); string tok; while (iss >> tok) out.push_back(tok); return out;
}
static inline string toUpper(const string &s) { string r=s; for (auto &c:r) c=toupper((unsigned char)c); return r; }
static inline string hexPad(uint64_t v, int width) { stringstream ss; ss<<uppercase<<hex<<setw(width)<<setfill('0')<<v; return ss.str(); }
static inline int hexStrToInt(const string &s) {
    string t=s; if (t.size()>=2 && t[0]=='0' && (t[1]=='x'||t[1]=='X')) t=t.substr(2);
    int v=0; stringstream ss; ss<<hex<<t; ss>>v; return v;
}
static inline bool isNumberToken(const string &s) {
    if (s.empty()) return false;
    size_t i=0; if (s[0]=='+'||s[0]=='-') i=1;
    for (; i<s.size(); ++i) if (!isdigit((unsigned char)s[i])) return false;
    return true;
}
static inline vector<string> splitByChar(const string &s, char c) {
    vector<string> out; string cur; for (char ch: s) { if (ch==c) { out.push_back(cur); cur.clear(); } else cur.push_back(ch); } out.push_back(cur); return out;
}

// ---------- OPTAB ----------
struct OptEntry { string mnemonic; uint8_t opcode; };
unordered_map<string, OptEntry> OPTAB;
bool loadOptab(const string &fname) {
    ifstream ifs(fname); if (!ifs) return false;
    string line;
    while (getline(ifs, line)) {
        line = trim(line);
        if (line.empty()) continue;
        if (line[0]=='.') continue;
        auto toks = split_ws(line);
        if (toks.size() < 2) continue;
        string m = toUpper(toks[0]); string ophex = toks[1];
        int val = hexStrToInt(ophex);
        OPTAB[m] = OptEntry{m, (uint8_t)val};
    }
    return true;
}

// ---------- CPU formats & registers ----------
unordered_map<string,int> REGNUM = {{"A",0},{"X",1},{"L",2},{"B",3},{"S",4},{"T",5},{"F",6},{"PC",8},{"SW",9}};
unordered_set<string> FORMAT1 = {"FIX","FLOAT","NORM","SIO","HIO","TIO"};
unordered_set<string> FORMAT2 = {"ADDR","COMPR","CLEAR","TIXR","RMO","SVC","SHIFTL","SHIFTR","MULR","DIVR","SUBR","SSK","TIXR","SVC"};

// ---------- Data structures ----------
struct SymEntry { string name; uint32_t addr; string block; bool isAbsolute; };
map<string, SymEntry> SYMTAB;

struct LitEntry {
    string hexKey;
    string firstToken;
    vector<uint8_t> bytes;
    uint32_t length;
    bool hasAddr;
    string block;
    uint32_t addr;
    int firstLineEncounter;
    LitEntry(): length(0), hasAddr(false), addr(0), firstLineEncounter(INT_MAX) {}
};
vector<LitEntry> LIT_LIST;
unordered_map<string,int> LIT_KEY_TO_IDX;
unordered_map<string,string> LITERAL_TOKEN_MAP;

struct Block { string name; uint32_t locctr; uint32_t length; uint32_t startAddr; bool used; };
vector<string> blockOrder;
unordered_map<string, Block> BLOCKTAB;

struct IntLine {
    int lineNo;
    string label;
    string opcode;
    string operand;
    string raw;
    bool comment;
    string block;
    uint32_t addr; // relative to block
    bool generatedObject;
    string objectCode;
};
vector<IntLine> INTLINES;

uint32_t programStart = 0;
string programName = "      ";
string startBlockName = "DEFAULT";
string END_LABEL = "";

vector<string> ERRORS;
void logError(int lineNo, const string &msg) { stringstream ss; ss << "Line " << lineNo << ": " << msg; ERRORS.push_back(ss.str()); }

// ---------- parse source ----------
vector<IntLine> parseSourceFile(const string &fname) {
    vector<IntLine> out;
    ifstream ifs(fname);
    if (!ifs) { cerr << "Cannot open source file: " << fname << "\n"; return out; }
    string raw; int lineno=0;
    while (getline(ifs, raw)) {
        ++lineno;
        string line = raw; if (!line.empty() && line.back()=='\r') line.pop_back();
        IntLine rec; rec.lineNo = lineno; rec.raw = line; rec.comment=false; rec.label=""; rec.opcode=""; rec.operand="";
        string t = trim(line);
        if (t.empty()) { rec.comment=true; out.push_back(rec); continue; }
        if (t[0]=='.') { rec.comment=true; out.push_back(rec); continue; }
        size_t firstNon = line.find_first_not_of(" \t");
        bool hasLabel = (firstNon == 0);
        if (hasLabel) {
            istringstream iss(line);
            string lab; iss >> lab; rec.label = trim(lab);
            string rest; getline(iss, rest); rest = trim(rest);
            if (!rest.empty()) {
                istringstream iss2(rest); string op; iss2 >> op; rec.opcode = toUpper(trim(op));
                string rem; getline(iss2, rem); rec.operand = trim(rem);
            }
        } else {
            string rest = trim(line);
            istringstream iss(rest); string op; iss >> op; rec.opcode = toUpper(trim(op));
            string rem; getline(iss, rem); rec.operand = trim(rem);
        }
        out.push_back(rec);
    }
    return out;
}

// ---------- literal helpers ----------
vector<uint8_t> bytesFromConstant(const string &operand, bool &ok) {
    ok=false; vector<uint8_t> res;
    if (operand.size() == 0) return res;
    string s = operand;
    if (s.size() >= 3 && (s[0]=='C'||s[0]=='c') && s[1]=='\'' && s.back()=='\'') {
        string inner = s.substr(2, s.size()-3);
        for (char c : inner) res.push_back((uint8_t)c);
        ok=true; return res;
    } else if (s.size() >= 3 && (s[0]=='X'||s[0]=='x') && s[1]=='\'' && s.back()=='\'') {
        string inner = s.substr(2, s.size()-3);
        if ((inner.size()%2)!=0) { ok=false; return res; }
        for (size_t i=0;i<inner.size(); i+=2) { string hb = inner.substr(i,2); int v = hexStrToInt(hb); res.push_back((uint8_t)v); }
        ok=true; return res;
    } else {
        try {
            long long v = stoll(s);
            res.push_back((v>>16)&0xFF); res.push_back((v>>8)&0xFF); res.push_back(v&0xFF);
            ok=true; return res;
        } catch(...) { ok=false; return res; }
    }
}
string bytesToHexKey(const vector<uint8_t> &bytes) {
    stringstream ss; ss<<uppercase<<hex;
    for (auto b: bytes) ss<<setw(2)<<setfill('0')<<(int)b;
    return ss.str();
}

// ---------- Expression evaluator ----------
struct EvalResult { bool ok; uint32_t value; bool isAbsolute; string err; };
// See earlier analysis: allow numbers, symbols, '*' and rules for relativity
EvalResult evalExpression(const string &expr, const string &currBlock, uint32_t currLocctr, int lineNo) {
    string s = trim(expr);
    if (s.empty()) return {false,0,false,"empty expression"};
    vector<pair<char,string>> terms;
    size_t i=0; char sign = '+';
    while (i < s.size()) {
        while (i<s.size() && isspace((unsigned char)s[i])) ++i;
        if (i>=s.size()) break;
        if (s[i] == '+' || s[i] == '-') { sign = s[i]; ++i; while (i<s.size() && isspace((unsigned char)s[i])) ++i; }
        size_t j = i; while (j<s.size() && s[j] != '+' && s[j] != '-') ++j;
        string token = trim(s.substr(i, j-i));
        if (token.empty()) return {false,0,false,"bad token in expression"};
        terms.push_back({sign, token});
        sign = '+';
        i = j;
    }
    long long acc = 0;
    int relativeCount = 0;
    vector<pair<char, tuple<uint32_t,bool,string>>> evaluated;
    for (auto &p : terms) {
        char sg = p.first; string tok = p.second;
        uint32_t val = 0; bool isAbsTerm = false; string termBlock="";
        if (tok == "*") {
            val = currLocctr; isAbsTerm = false; termBlock = currBlock;
        } else if (isNumberToken(tok)) {
            try { val = (uint32_t)stoul(tok,nullptr,0); isAbsTerm = true; } catch(...) { return {false,0,false,"bad numeric: "+tok}; }
        } else {
            auto it = SYMTAB.find(toUpper(tok));
            if (it == SYMTAB.end()) return {false,0,false,"Undefined symbol '" + tok + "'"};
            val = it->second.addr; isAbsTerm = it->second.isAbsolute; termBlock = it->second.block;
        }
        evaluated.push_back({sg, make_tuple(val, isAbsTerm, termBlock)});
        if (!isAbsTerm) ++relativeCount;
    }
    if (relativeCount >= 3) return {false,0,false,"Too many relative terms"};
    if (relativeCount == 2) {
        int idx=0; vector<int> relIdx;
        for (auto &e : evaluated) { if (!get<1>(e.second)) relIdx.push_back(idx); ++idx; }
        if (relIdx.size() != 2) return {false,0,false,"internal expr"};
        auto &t1 = evaluated[relIdx[0]]; auto &t2 = evaluated[relIdx[1]];
        string b1 = get<2>(t1.second), b2 = get<2>(t2.second);
        char s1 = t1.first, s2 = t2.first;
        if (toUpper(b1) != toUpper(b2)) return {false,0,false,"Relative terms from different blocks"};
        if (!((s1=='+' && s2=='-') || (s1=='-' && s2=='+'))) return {false,0,false,"Relative terms must be opposite signs"};
    }
    for (auto &e : evaluated) {
        char sgn = e.first;
        uint32_t v = get<0>(e.second);
        if (sgn == '+') acc += (long long)v; else acc -= (long long)v;
    }
    bool resultAbs = (relativeCount == 0) || (relativeCount == 2);
    return {true, (uint32_t)acc, resultAbs, ""};
}

// ---------- PASS1 ----------
void processLiteralPool_upToLine(uint32_t &locctr, const string &currBlock, int currentLine) {
    for (size_t i=0;i<LIT_LIST.size(); ++i) {
        auto &lit = LIT_LIST[i];
        if (!lit.hasAddr && lit.firstLineEncounter <= currentLine) {
            lit.hasAddr = true; lit.block = currBlock; lit.addr = locctr;
            locctr += lit.length;
            IntLine r; r.lineNo = 0; r.label=""; r.opcode="=LITERAL"; r.operand = lit.firstToken;
            r.raw = lit.firstToken; r.comment=false; r.block = currBlock; r.addr = lit.addr; r.generatedObject=true; r.objectCode="";
            INTLINES.push_back(r);
        }
    }
}

void doPass1(const string &srcFile) {
    SYMTAB.clear(); LIT_LIST.clear(); LIT_KEY_TO_IDX.clear(); LITERAL_TOKEN_MAP.clear();
    INTLINES.clear(); BLOCKTAB.clear(); blockOrder.clear(); ERRORS.clear();
    programStart = 0; programName = "      ";

    BLOCKTAB[startBlockName] = Block{startBlockName,0,0,0,true};
    blockOrder.push_back(startBlockName);
    string currBlock = startBlockName;
    uint32_t locctr = 0;
    bool started = false;

    auto ensureBlock = [&](const string &bname){ string bn = bname.empty()? startBlockName : bname; if (BLOCKTAB.find(bn) == BLOCKTAB.end()) { BLOCKTAB[bn] = Block{bn,0,0,0,true}; blockOrder.push_back(bn);} };

    vector<IntLine> parsed = parseSourceFile(srcFile);
    int lineno=0;
    for (auto &pline : parsed) {
        ++lineno;
        IntLine rec = pline; rec.block = currBlock; rec.addr = locctr; rec.generatedObject=false; rec.objectCode="";
        if (rec.comment) { INTLINES.push_back(rec); continue; }
        string op = trim(rec.opcode); string operand = trim(rec.operand);

        if (!started && op == "START") {
            started = true;
            programName = rec.label.empty()? "      " : rec.label;
            uint32_t st = 0;
            if (!operand.empty()) { try { st = (uint32_t)stoul(operand,nullptr,0); } catch(...) { st = 0; } }
            programStart = st;
            locctr = 0; BLOCKTAB[currBlock].locctr = locctr;
            rec.addr = locctr; INTLINES.push_back(rec); continue;
        }

        if (op == "USE") {
            BLOCKTAB[currBlock].locctr = locctr;
            string newBlock = operand.empty()? startBlockName : operand;
            ensureBlock(newBlock);
            currBlock = newBlock; locctr = BLOCKTAB[currBlock].locctr;
            rec.block = currBlock; rec.addr = locctr; INTLINES.push_back(rec); continue;
        }

        if (op == "ORG") {
            rec.addr = locctr;
            if (!operand.empty()) {
                bool ok=false; uint32_t val=0;
                if (isNumberToken(operand)) { try { val=(uint32_t)stoul(operand,nullptr,0); ok=true; } catch(...) { ok=false; } }
                else {
                    auto it = SYMTAB.find(toUpper(operand));
                    if (it != SYMTAB.end()) { val = it->second.addr; ok=true; }
                    else {
                        auto ev = evalExpression(operand, currBlock, locctr, rec.lineNo);
                        if (ev.ok) { val = ev.value; ok = true; } else logError(rec.lineNo, "ORG expr failed: " + operand);
                    }
                }
                if (ok) { locctr = val; BLOCKTAB[currBlock].locctr = locctr; }
            }
            INTLINES.push_back(rec); continue;
        }

        if (op == "EQU") {
            if (rec.label.empty()) logError(rec.lineNo, "EQU without label");
            else {
                if (operand.empty()) {
                    SYMTAB[toUpper(rec.label)] = SymEntry{toUpper(rec.label), locctr, currBlock, true};
                } else {
                    if (isNumberToken(operand)) {
                        uint32_t v = (uint32_t)stoul(operand,nullptr,0);
                        SYMTAB[toUpper(rec.label)] = SymEntry{toUpper(rec.label), v, currBlock, true};
                    } else {
                        auto it = SYMTAB.find(toUpper(operand));
                        if (it != SYMTAB.end()) {
                            SYMTAB[toUpper(rec.label)] = SymEntry{toUpper(rec.label), it->second.addr, it->second.block, it->second.isAbsolute};
                        } else {
                            auto ev = evalExpression(operand, currBlock, locctr, rec.lineNo);
                            if (ev.ok) SYMTAB[toUpper(rec.label)] = SymEntry{toUpper(rec.label), ev.value, currBlock, ev.isAbsolute};
                            else logError(rec.lineNo, "EQU eval failed: " + operand);
                        }
                    }
                }
            }
            rec.addr = locctr; INTLINES.push_back(rec); continue;
        }

        if (op == "LTORG") {
            rec.addr = locctr; INTLINES.push_back(rec);
            processLiteralPool_upToLine(locctr, currBlock, rec.lineNo);
            BLOCKTAB[currBlock].locctr = locctr; continue;
        }

        if (op == "END") {
            rec.addr = locctr; INTLINES.push_back(rec);
            processLiteralPool_upToLine(locctr, currBlock, rec.lineNo);
            BLOCKTAB[currBlock].locctr = locctr; break;
            if (!operand.empty()) {
                END_LABEL = trim(operand); // store end entry label for pass2
            }
        }

        if (op == "BASE") { rec.addr = locctr; INTLINES.push_back(rec); continue; }

        if (!rec.label.empty()) {
            string lab = toUpper(rec.label);
            if (SYMTAB.find(lab) != SYMTAB.end()) logError(rec.lineNo, "Duplicate symbol: " + lab);
            else SYMTAB[lab] = SymEntry{lab, locctr, currBlock, false};
        }

        // literal detection
        if (!operand.empty()) {
            string opnd = operand; size_t commaPos = opnd.find(','); if (commaPos!=string::npos) opnd = trim(opnd.substr(0, commaPos));
            if (!opnd.empty() && opnd[0]=='=') {
                string litToken = opnd;
                string litVal = litToken.substr(1);
                bool ok=false; vector<uint8_t> bytes = bytesFromConstant(litVal, ok);
                if (!ok) {
                    vector<uint8_t> b; long long v=0; try { v = stoll(litVal); } catch(...) { v=0; }
                    b.push_back((v>>16)&0xFF); b.push_back((v>>8)&0xFF); b.push_back(v&0xFF); bytes = b; ok=true;
                }
                string hk = bytesToHexKey(bytes);
                if (LIT_KEY_TO_IDX.find(hk) == LIT_KEY_TO_IDX.end()) {
                    LitEntry ent; ent.hexKey = hk; ent.firstToken = litToken; ent.bytes = bytes; ent.length = (uint32_t)bytes.size();
                    ent.hasAddr = false; ent.block=""; ent.addr=0; ent.firstLineEncounter = rec.lineNo;
                    LIT_LIST.push_back(ent); LIT_KEY_TO_IDX[hk] = (int)LIT_LIST.size()-1;
                } else {
                    int idx = LIT_KEY_TO_IDX[hk];
                    if (LIT_LIST[idx].firstLineEncounter > rec.lineNo) LIT_LIST[idx].firstLineEncounter = rec.lineNo;
                }
                if (LITERAL_TOKEN_MAP.find(litToken) == LITERAL_TOKEN_MAP.end()) LITERAL_TOKEN_MAP[litToken] = hk;
            }
        }

        // LOCCTR increment
        uint32_t inc = 0;
        bool isFormat4 = false;
        string opcodeToken = op;
        if (!opcodeToken.empty() && opcodeToken[0] == '+') { isFormat4 = true; opcodeToken = opcodeToken.substr(1); }
        if (OPTAB.find(opcodeToken) != OPTAB.end()) {
            if (isFormat4) inc = 4;
            else {
                if (FORMAT1.find(opcodeToken) != FORMAT1.end()) inc = 1;
                else if (FORMAT2.find(opcodeToken) != FORMAT2.end()) inc = 2;
                else inc = 3;
            }
        } else {
            if (op == "WORD") inc = 3;
            else if (op == "RESW") { try { int n = stoi(operand); inc = 3U * (uint32_t)n; } catch(...) { inc=0; logError(rec.lineNo,"Invalid RESW"); } }
            else if (op == "RESB") { try { int n = stoi(operand); inc = (uint32_t)n; } catch(...) { inc=0; logError(rec.lineNo,"Invalid RESB"); } }
            else if (op == "BYTE") {
                if (operand.size()>=3 && (operand[0]=='C'||operand[0]=='c') && operand[1]=='\'' && operand.back()=='\'') { string inner = operand.substr(2, operand.size()-3); inc = (uint32_t)inner.size(); }
                else if (operand.size()>=3 && (operand[0]=='X'||operand[0]=='x') && operand[1]=='\'' && operand.back()=='\'') { string inner = operand.substr(2, operand.size()-3); inc = (uint32_t)(inner.size()/2); }
                else inc = 1;
            } else { logError(rec.lineNo, "Invalid opcode/directive: " + op); inc = 0; }
        }

        rec.addr = locctr;
        INTLINES.push_back(rec);
        locctr += inc;
        BLOCKTAB[currBlock].locctr = locctr;
    } // end parsed

    // compute block lengths and start addresses
    for (auto &bn : blockOrder) if (BLOCKTAB.find(bn) != BLOCKTAB.end()) BLOCKTAB[bn].length = BLOCKTAB[bn].locctr;
    uint32_t curAbs = programStart;
    for (auto &bn : blockOrder) { BLOCKTAB[bn].startAddr = curAbs; curAbs += BLOCKTAB[bn].length; }

    // write INTFILE
    ofstream intf("INTFILE.txt");
    for (auto &r : INTLINES) {
        if (r.comment) { intf << setw(4) << r.lineNo << "    " << r.raw << "\n"; continue; }
        uint32_t absAddr = 0;
        if (toUpper(r.opcode) == "START") absAddr = programStart;
        else if (BLOCKTAB.find(r.block) != BLOCKTAB.end()) absAddr = BLOCKTAB[r.block].startAddr + r.addr;
        else absAddr = r.addr;
        intf << setw(4) << (r.lineNo>0? r.lineNo:0) << " " << setw(6) << hexPad(absAddr,6) << " [" << r.block << "] ";
        if (!r.label.empty()) intf << setw(8) << r.label << " "; else intf << setw(8) << " " << " ";
        intf << setw(8) << r.opcode; if (!r.operand.empty()) intf << " " << r.operand; intf << "\n";
    }
    intf.close();

    ofstream symf("SYMTAB.txt");
    for (auto &p : SYMTAB) symf << p.first << " " << hexPad(p.second.addr,6) << " " << p.second.block << (p.second.isAbsolute?" ABS":"") << "\n";
    symf.close();

    ofstream litf("LITTAB.txt");
    for (size_t i=0;i<LIT_LIST.size(); ++i) {
        auto &le = LIT_LIST[i];
        litf << i << " " << le.hexKey << " token=" << le.firstToken << " len=" << le.length << " addr=" << (le.hasAddr?hexPad(le.addr,6):string("UNDEF")) << " block=" << le.block << " firstLine=" << le.firstLineEncounter << "\n";
    }
    litf.close();

    cout << "=== PASS1 complete ===\n";
    cout << "Program start: " << hexPad(programStart,6) << " Name: " << programName << "\n";
}

// ---------- PASS2 helpers ----------
uint32_t computeAbsAddrSymbol(const string &sym, bool &ok) {
    ok = false;
    auto it = SYMTAB.find(toUpper(sym));
    if (it != SYMTAB.end()) {
        if (BLOCKTAB.find(it->second.block) == BLOCKTAB.end()) return 0;
        ok = true;
        if (it->second.isAbsolute) return it->second.addr; // absolute stored value
        return BLOCKTAB[it->second.block].startAddr + it->second.addr;
    }
    try { uint32_t v = (uint32_t)stoul(sym,nullptr,0); ok=true; return v; } catch(...) { ok=false; return 0; }
}
string buildFormat1(uint8_t opcode) { stringstream ss; ss<<uppercase<<hex<<setw(2)<<setfill('0')<<(int)opcode; return ss.str(); }
string buildFormat2(uint8_t opcode, int r1, int r2) { uint8_t b0 = opcode; uint8_t b1 = ((r1&0xF)<<4) | (r2 & 0xF); stringstream ss; ss<<uppercase<<hex<<setw(2)<<setfill('0')<<(int)b0<<setw(2)<<(int)b1; return ss.str(); }
string buildFormat34(uint8_t opcode, bool n,bool i,bool x,bool b,bool p,bool e,uint32_t disp_or_addr) {
    uint8_t byte0 = (opcode & 0xFC) | ( ((n?1:0)<<1) | (i?1:0) );
    if (!e) {
        uint16_t disp12 = (uint16_t)(disp_or_addr & 0xFFF);
        uint8_t flags = ((x?1:0)<<3) | ((b?1:0)<<2) | ((p?1:0)<<1) | (e?1:0);
        uint8_t byte1 = (flags << 4) | ((disp12 >> 8) & 0x0F);
        uint8_t byte2 = disp12 & 0xFF;
        stringstream ss; ss<<uppercase<<hex<<setw(2)<<setfill('0')<<(int)byte0<<setw(2)<<(int)byte1<<setw(2)<<(int)byte2; return ss.str();
    } else {
        uint32_t addr20 = disp_or_addr & 0xFFFFF;
        uint8_t flags = ((x?1:0)<<3) | ((b?1:0)<<2) | ((p?1:0)<<1) | (e?1:0);
        uint8_t byte1 = (flags << 4) | ((addr20 >> 16) & 0x0F);
        uint8_t byte2 = (addr20 >> 8) & 0xFF;
        uint8_t byte3 = addr20 & 0xFF;
        stringstream ss; ss<<uppercase<<hex<<setw(2)<<setfill('0')<<(int)byte0<<setw(2)<<(int)byte1<<setw(2)<<(int)byte2<<setw(2)<<(int)byte3; return ss.str();
    }
}
vector<uint8_t> hexStrToBytes(const string &hexs) {
    vector<uint8_t> out;
    for (size_t i=0;i+1<hexs.size(); i+=2) {
        string hb = hexs.substr(i,2);
        int v = hexStrToInt(hb);
        out.push_back((uint8_t)v);
    }
    return out;
}

// ---------- PASS2 ----------
void doPass2(const string &srcFile) {
    // assign block start addresses sequentially
    uint32_t curAddr = programStart;
    for (auto &bn : blockOrder) {
        BLOCKTAB[bn].startAddr = curAddr;
        curAddr += BLOCKTAB[bn].length;
    }
    uint32_t programLength = curAddr - programStart;

    // track base register state as we iterate INTLINES
    bool baseOn = false;
    uint32_t baseValue = 0;

    // generate object codes per INTLINE sequentially (so BASE updates are in-order)
    for (auto &r : INTLINES) {
        r.generatedObject = false; r.objectCode = "";
        if (r.comment) continue;
        string op = toUpper(r.opcode);
        string operand = trim(r.operand);

        if (op=="START" || op=="END" || op=="LTORG" || op=="USE" || op=="ORG" || op=="EQU") continue;

        if (op == "BASE") {
            if (!operand.empty()) {
                bool ok=false; uint32_t a = computeAbsAddrSymbol(operand, ok);
                if (ok) { baseOn = true; baseValue = a; } else { logError(r.lineNo, "BASE unresolved: "+operand); baseOn=false; }
            } else { baseOn = false; }
            continue;
        }

        if (op == "=LITERAL") {
            string tok = operand;
            if (LITERAL_TOKEN_MAP.find(tok) != LITERAL_TOKEN_MAP.end()) {
                string hk = LITERAL_TOKEN_MAP[tok];
                int idx = LIT_KEY_TO_IDX[hk];
                stringstream ss; for (auto b : LIT_LIST[idx].bytes) ss<<uppercase<<hex<<setw(2)<<setfill('0')<<(int)b;
                r.objectCode = ss.str(); r.generatedObject=true;
            } else logError(r.lineNo, "Unknown literal token in pass2: "+tok);
            continue;
        }

        if (op == "WORD") {
            uint32_t v=0;
            if (!operand.empty()) {
                if (isNumberToken(operand)) v=(uint32_t)stoul(operand,nullptr,0);
                else { bool ok=false; uint32_t a = computeAbsAddrSymbol(operand, ok); if (ok) v=a; else logError(r.lineNo,"WORD unresolved: "+operand); }
            }
            r.objectCode = hexPad(v,6); r.generatedObject = true; continue;
        }
        if (op == "BYTE") {
            if (operand.size()>=3 && (operand[0]=='C'||operand[0]=='c') && operand[1]=='\'' && operand.back()=='\'') {
                string inner = operand.substr(2, operand.size()-3);
                stringstream ss; for (char c : inner) ss<<uppercase<<hex<<setw(2)<<setfill('0')<<(int)((unsigned char)c);
                r.objectCode = ss.str(); r.generatedObject=true;
            } else if (operand.size()>=3 && (operand[0]=='X'||operand[0]=='x') && operand[1]=='\'' && operand.back()=='\'') {
                string inner = operand.substr(2, operand.size()-3);
                r.objectCode = toUpper(inner); r.generatedObject=true;
            } else {
                try { uint32_t v=(uint32_t)stoul(operand,nullptr,0); r.objectCode = hexPad(v,2); r.generatedObject=true; }
                catch(...) { logError(r.lineNo,"BYTE parse fail: "+operand); }
            }
            continue;
        }
        if (op == "RESW" || op == "RESB") continue;

        bool isFormat4 = false;
        string opClean = op;
        if (!opClean.empty() && opClean[0] == '+') { isFormat4 = true; opClean = opClean.substr(1); }
        if (OPTAB.find(opClean) == OPTAB.end()) { logError(r.lineNo, "Undefined opcode: " + opClean); continue; }
        uint8_t opcode = OPTAB[opClean].opcode;

        if (FORMAT1.find(opClean) != FORMAT1.end()) { r.objectCode = buildFormat1(opcode); r.generatedObject=true; continue; }
        if (FORMAT2.find(opClean) != FORMAT2.end()) {
            int r1=-1,r2=0;
            if (!operand.empty()) {
                auto parts = splitByChar(operand, ',');
                if (parts.size()>=1) { string p1 = trim(parts[0]); if (REGNUM.find(toUpper(p1)) != REGNUM.end()) r1 = REGNUM[toUpper(p1)]; else { try { r1 = stoi(p1);} catch(...) { r1=0; } } }
                if (parts.size()>=2) { string p2 = trim(parts[1]); if (REGNUM.find(toUpper(p2)) != REGNUM.end()) r2 = REGNUM[toUpper(p2)]; else { try { r2 = stoi(p2);} catch(...) { r2=0; } } }
            }
            r.objectCode = buildFormat2(opcode, r1, r2); r.generatedObject=true; continue;
        }

        // format 3/4
        bool n=false,i=false,x=false,b=false,p=false,e=false;
        string oper = operand;
        if (!oper.empty() && oper[0] == '#') { i=true; n=false; oper = oper.substr(1); }
        else if (!oper.empty() && oper[0] == '@') { n=true; i=false; oper = oper.substr(1); }
        else { n=true; i=true; }
        string operNoIndex = oper;
        if (!operNoIndex.empty()) {
            size_t comma = operNoIndex.find(',');
            if (comma != string::npos) {
                string after = trim(operNoIndex.substr(comma+1));
                if (toUpper(after) == "X") { x=true; operNoIndex = trim(operNoIndex.substr(0, comma)); }
            }
        }
        if (isFormat4) e = true;

        bool immediateNumeric = false; uint32_t immediateValue = 0;
        if (i && !operNoIndex.empty() && isNumberToken(operNoIndex)) {
            try { immediateValue = (uint32_t)stoul(operNoIndex,nullptr,0); immediateNumeric=true; } catch(...) { immediateNumeric=false; }
        }
        if (immediateNumeric && !operNoIndex.empty()) {
            if (!isFormat4 && immediateValue > 0xFFF) { isFormat4 = true; e = true; }
            r.objectCode = buildFormat34(opcode, n,i,x,false,false,e, immediateValue);
            r.generatedObject=true; continue;
        }

        bool operandIsLiteral = (!operNoIndex.empty() && operNoIndex[0] == '=');
        uint32_t targetAbs = 0; bool okTarget=false; bool targetIsAbsoluteSymbol=false;
        if (operandIsLiteral) {
            string litTok = operNoIndex;
            if (LITERAL_TOKEN_MAP.find(litTok) != LITERAL_TOKEN_MAP.end()) {
                string hk = LITERAL_TOKEN_MAP[litTok];
                int idx = LIT_KEY_TO_IDX[hk];
                if (LIT_LIST[idx].hasAddr) { targetAbs = BLOCKTAB[LIT_LIST[idx].block].startAddr + LIT_LIST[idx].addr; okTarget=true; }
                else { logError(r.lineNo, "Literal not placed yet: " + litTok); okTarget=false; }
            } else { logError(r.lineNo, "Literal token unknown: " + litTok); okTarget=false; }
        } else if (!operNoIndex.empty()) {
            // check SYMTAB first to get isAbsolute info
            auto it = SYMTAB.find(toUpper(operNoIndex));
            if (it != SYMTAB.end()) {
                if (it->second.isAbsolute) { targetIsAbsoluteSymbol = true; targetAbs = it->second.addr; okTarget=true; }
                else { targetIsAbsoluteSymbol = false; targetAbs = BLOCKTAB[it->second.block].startAddr + it->second.addr; okTarget=true; }
            } else {
                // maybe numeric literal like '123' (should have been caught earlier), or undefined
                bool ok=false; uint32_t a = computeAbsAddrSymbol(operNoIndex, ok);
                if (ok) { targetAbs = a; okTarget=true; targetIsAbsoluteSymbol = true; } else { okTarget=false; }
            }
        } else okTarget=false;

        if (!okTarget && !(opClean == "RSUB")) { logError(r.lineNo, "Undefined operand: " + operNoIndex); continue; }

        if (opClean == "RSUB") {
            r.objectCode = buildFormat34(opcode, true, true, false, false, false, false, 0);
            r.generatedObject=true; continue;
        }

        uint32_t instrAbs = BLOCKTAB[r.block].startAddr + r.addr;
        int instrLen = isFormat4 ? 4 : 3;

        // If operand is a symbol and it's absolute in SYMTAB, prefer direct 12-bit encoding (p=b=0) if it fits.
        if (!isFormat4 && okTarget && !operandIsLiteral && (!immediateNumeric) && targetIsAbsoluteSymbol) {
            if (targetAbs <= 0xFFF) {
                p = false; b = false;
                r.objectCode = buildFormat34(opcode, n, i, x, b, p, false, targetAbs);
                r.generatedObject = true;
                continue;
            } else {
                // too big -> fall through to format4
                isFormat4 = true; e = true; instrLen = 4;
            }
        }

        if (!isFormat4) {
            int32_t disp = (int32_t)targetAbs - (int32_t)(instrAbs + 3);
            if (disp >= -2048 && disp <= 2047) {
                p = true; b = false;
                uint32_t disp12 = (uint32_t)(disp & 0xFFF);
                r.objectCode = buildFormat34(opcode, n, i, x, b, p, false, disp12);
                r.generatedObject = true; continue;
            } else {
                if (baseOn) {
                    int32_t dispb = (int32_t)targetAbs - (int32_t)baseValue;
                    if (dispb >= 0 && dispb <= 4095) {
                        b = true; p = false;
                        uint32_t disp12 = (uint32_t)(dispb & 0xFFF);
                        r.objectCode = buildFormat34(opcode, n, i, x, b, p, false, disp12);
                        r.generatedObject = true; continue;
                    } else {
                        isFormat4 = true; e = true; instrLen = 4;
                    }
                } else {
                    isFormat4 = true; e = true; instrLen = 4;
                }
            }
        }

        if (isFormat4) {
            r.objectCode = buildFormat34(opcode, n, i, x, false, false, true, targetAbs);
            r.generatedObject = true; continue;
        }
    } // end INTLINES

    // Build per-block address->byte map
    unordered_map<string, map<uint32_t,uint8_t>> blockByteMap;
    for (auto &r : INTLINES) {
        if (r.generatedObject && !r.objectCode.empty()) {
            uint32_t abs = BLOCKTAB[r.block].startAddr + r.addr;
            auto bytes = hexStrToBytes(r.objectCode);
            for (size_t i=0;i<bytes.size(); ++i) {
                uint32_t a = abs + (uint32_t)i;
                auto &m = blockByteMap[r.block];
                if (m.find(a) != m.end()) {
                    logError(r.lineNo, "Byte overlap at address " + hexPad(a,6) + " in block " + r.block);
                }
                m[a] = bytes[i];
            }
        }
    }
    // add literal bytes
    for (size_t i=0;i<LIT_LIST.size(); ++i) {
        auto &lit = LIT_LIST[i];
        if (!lit.hasAddr) continue;
        uint32_t abs = BLOCKTAB[lit.block].startAddr + lit.addr;
        auto &m = blockByteMap[lit.block];
        for (size_t j=0;j<lit.bytes.size(); ++j) {
            uint32_t a = abs + (uint32_t)j;
            if (m.find(a) != m.end()) logError(0, "Literal overlap at " + hexPad(a,6));
            m[a] = lit.bytes[j];
        }
    }

    // Build M records list (format4 entries)
    vector<pair<uint32_t,int>> MRECS;
    for (auto &r : INTLINES) {
        if (r.generatedObject && r.objectCode.size() == 8) {
            uint32_t abs = BLOCKTAB[r.block].startAddr + r.addr;
            MRECS.push_back({abs+1, 5});
        }
    }

    // Write OBJ file
    ofstream objf("OBJFILE.obj");
    string pname = programName; if (pname.size() > 6) pname = pname.substr(0,6); else pname += string(6 - pname.size(), ' ');
    objf << "H" << pname << hexPad(programStart,6) << hexPad(programLength,6) << "\n";
    cout << "H" << pname << hexPad(programStart,6) << hexPad(programLength,6) << "\n";

    for (auto &bn : blockOrder) {
        auto itmap = blockByteMap.find(bn);
        if (itmap == blockByteMap.end()) continue;
        auto &m = itmap->second;
        if (m.empty()) continue;
        auto it = m.begin();
        while (it != m.end()) {
            uint32_t startAddr = it->first;
            vector<uint8_t> buf;
            uint32_t cur = startAddr;
            while (it != m.end() && it->first == cur && buf.size() < 30) {
                buf.push_back(it->second);
                ++it; ++cur;
            }
            stringstream ss;
            for (auto b : buf) ss<<uppercase<<hex<<setw(2)<<setfill('0')<<(int)b;
            string hexs = ss.str();
            objf << "T" << hexPad(startAddr,6) << hexPad((uint32_t)buf.size(),2) << hexs << "\n";
            cout << "T" << hexPad(startAddr,6) << hexPad((uint32_t)buf.size(),2) << hexs << "\n";
        }
    }

    for (auto &m : MRECS) {
        objf << "M" << hexPad(m.first,6) << hexPad(m.second,2) << "\n";
        cout << "M" << hexPad(m.first,6) << hexPad(m.second,2) << "\n";
    }

    uint32_t entryAddr = programStart;
    if (!END_LABEL.empty()) {
        bool ok=false; uint32_t a = computeAbsAddrSymbol(END_LABEL, ok);
        if (ok) entryAddr = a;
        else logError(0, "END entry symbol unresolved: " + END_LABEL);
    }
    objf << "E" << hexPad(entryAddr,6) << "\n";
    cout << "E" << hexPad(entryAddr,6) << "\n";

    objf.close();

    cout << "=== PASS2 complete ===\n";
    cout << "Program length: " << hexPad(programLength,6) << "\n";
    if (!ERRORS.empty()) {
        cout << "Errors/Warnings:\n";
        for (auto &e : ERRORS) cout << e << "\n";
    }
    cout << "Wrote OBJFILE.obj, INTFILE.txt, SYMTAB.txt, LITTAB.txt\n";
}

// ---------- main ----------
int main(int argc, char** argv) {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    cout << "SIC/XE 2-pass assembler (fixed absolute-symbol direct encoding)\n";
    string src;
    if (argc >= 2) src = argv[1];
    else { cout << "Enter source filename: " << flush; if (!getline(cin, src)) { cerr << "No input\n"; return 1; } src = trim(src); if (src.empty()) { cerr << "Empty filename\n"; return 1; } }
    if (!loadOptab("optab.txt")) { cerr << "Failed to load optab.txt\n"; return 2; }
    BLOCKTAB.clear(); blockOrder.clear(); BLOCKTAB[startBlockName] = Block{startBlockName,0,0,0,true}; blockOrder.push_back(startBlockName);
    doPass1(src);
    doPass2(src);
    return 0;
}
