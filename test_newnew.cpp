// assembler_fixed.cpp
// SIC/XE 2-pass Assembler (single-file) -- Literal handling fixes
// - Deduplicate literals by their byte (hex) value
// - Ensure LTORG assigns addresses at current LOCCTR and increments LOCCTR properly
// - Other features as previously implemented
// Compile: g++ -std=c++17 assembler_fixed.cpp -o assembler

#include <bits/stdc++.h>
using namespace std;

// ----------------------- Utility functions ------------------------------
static inline string trim(const string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
static inline vector<string> split_ws(const string &s) {
    vector<string> out;
    istringstream iss(s);
    string tok;
    while (iss >> tok) out.push_back(tok);
    return out;
}
static inline string toUpper(const string &s) {
    string r = s;
    for (auto &c : r) c = toupper((unsigned char)c);
    return r;
}
static inline string hexPad(uint64_t v, int width) {
    stringstream ss;
    ss << uppercase << hex;
    ss << setw(width) << setfill('0') << v;
    return ss.str();
}
static inline int hexStrToInt(const string &s) {
    string t = s;
    if (t.size() >= 2 && t[0] == '0' && (t[1]=='x' || t[1]=='X')) t = t.substr(2);
    int v=0; stringstream ss;
    ss << hex << t;
    ss >> v;
    return v;
}
static inline vector<string> splitByChar(const string &s, char c) {
    vector<string> out;
    string cur;
    for (char ch : s) {
        if (ch == c) { out.push_back(cur); cur.clear(); }
        else cur.push_back(ch);
    }
    out.push_back(cur);
    return out;
}

// ----------------------- OPTAB loader -----------------------------------
struct OptEntry {
    string mnemonic;
    uint8_t opcode; // 8-bit opcode
};
unordered_map<string, OptEntry> OPTAB;

bool loadOptab(const string &fname) {
    ifstream ifs(fname);
    if (!ifs) return false;
    string line;
    while (getline(ifs, line)) {
        line = trim(line);
        if (line.empty()) continue;
        if (line[0] == '.') continue;
        auto toks = split_ws(line);
        if (toks.size() < 2) continue;
        string m = toUpper(toks[0]);
        string ophex = toks[1];
        int val = hexStrToInt(ophex);
        OPTAB[m] = OptEntry{m, (uint8_t)val};
    }
    return true;
}

// ----------------------- Register map & formats -------------------------
unordered_map<string,int> REGNUM = {
    {"A",0},{"X",1},{"L",2},{"B",3},{"S",4},{"T",5},{"F",6},{"PC",8},{"SW",9}
};
unordered_set<string> FORMAT1 = {"FIX","FLOAT","NORM","SIO","HIO","TIO"};
unordered_set<string> FORMAT2 = {
    "ADDR","COMPR","CLEAR","TIXR","RMO","SVC","SHIFTL","SHIFTR","MULR","DIVR","SUBR"
};

// ----------------------- Data structures --------------------------------
struct SymEntry {
    string name;
    uint32_t addr;   // relative addr within block
    string block;    // block name
    bool isAbsolute; // true if EQU with absolute
};
map<string, SymEntry> SYMTAB;

// *** LITERAL FIX: LITTAB keyed by canonical hex string (byte sequence).
struct LitEntry {
    string hexKey;      // canonical hex representation of bytes, e.g., "414141"
    string firstToken;  // first token seen for listing, e.g., "=C'AAA'"
    vector<uint8_t> bytes;
    uint32_t length;
    bool hasAddr;
    string block;
    uint32_t addr;
};
unordered_map<string, LitEntry> LITTAB; // key = hexKey -> entry

// Map from literal token (=C'..', =X'..', =1234) to hexKey in LITTAB
unordered_map<string,string> LITERAL_TOKEN_MAP; // token -> hexKey

struct Block {
    string name;
    uint32_t locctr;
    uint32_t length;
    uint32_t startAddr; // assigned later in pass2
    bool used;
};
vector<string> blockOrder; // order encountered
unordered_map<string, Block> BLOCKTAB;

struct IntLine {
    int lineNo;
    string label;
    string opcode;
    string operand;
    string raw;
    bool comment;
    string block; // block name this line belongs to
    uint32_t addr; // address assigned (relative to block)
    bool generatedObject; // whether object code was created for this line
    string objectCode; // hex string
};
vector<IntLine> INTLINES;

uint32_t programStart = 0;
string programName;
string startBlockName = "DEFAULT";

// Error logging
vector<string> ERRORS;
void logError(int lineNo, const string &msg) {
    stringstream ss; ss << "Line " << lineNo << ": " << msg;
    ERRORS.push_back(ss.str());
}

// ----------------------- Source parsing ---------------------------------
vector<IntLine> parseSourceFile(const string &fname) {
    vector<IntLine> out;
    ifstream ifs(fname);
    if (!ifs) { cerr << "Cannot open source file: " << fname << "\n"; return out; }
    string raw;
    int lineno = 0;
    while (getline(ifs, raw)) {
        ++lineno;
        string line = raw;
        if (!line.empty() && line.back()=='\r') line.pop_back();
        IntLine rec; rec.lineNo = lineno; rec.raw = line; rec.comment = false;
        string t = trim(line);
        if (t.empty()) { rec.comment = true; out.push_back(rec); continue; }
        size_t firstNS = line.find_first_not_of(" \t");
        if (firstNS != string::npos && line[firstNS] == '.') { rec.comment = true; out.push_back(rec); continue; }

        bool hasLabel = !(line.size() > 0 && (line[0]==' '||line[0]=='\t'));
        if (hasLabel) {
            istringstream iss(line);
            string lab; iss >> lab;
            rec.label = trim(lab);
            size_t pos = line.find(lab);
            if (pos!=string::npos) {
                string rem = line.substr(pos + lab.size());
                rem = trim(rem);
                if (rem.empty()) { rec.opcode = ""; rec.operand = ""; out.push_back(rec); continue; }
                istringstream iss2(rem); string op; iss2 >> op;
                rec.opcode = trim(op);
                size_t oppos = rem.find(op);
                if (oppos!=string::npos) {
                    string oper = rem.substr(oppos + op.size());
                    rec.operand = trim(oper);
                }
            }
        } else {
            string rem = trim(line);
            istringstream iss(rem);
            string op; iss >> op;
            rec.opcode = toUpper(op);
            size_t pos = rem.find(op);
            if (pos!=string::npos) {
                string oper = rem.substr(pos + op.size());
                rec.operand = trim(oper);
            }
        }
        rec.opcode = toUpper(rec.opcode);
        out.push_back(rec);
    }
    return out;
}

// ----------------------- Literal byte parser -----------------------------
vector<uint8_t> bytesFromConstant(const string &operand, bool &ok) {
    ok = false;
    vector<uint8_t> res;
    if (operand.size() == 0) return res;
    string s = operand;
    if (s.size() > 2 && (s[0]=='C' || s[0]=='c') && s[1]=='\'' && s.back()=='\'') {
        string inside = s.substr(2, s.size()-3);
        for (char c : inside) res.push_back((uint8_t)c);
        ok = true; return res;
    } else if (s.size() > 2 && (s[0]=='X' || s[0]=='x') && s[1]=='\'' && s.back()=='\'') {
        string inside = s.substr(2, s.size()-3);
        if (inside.size() % 2 != 0) return res;
        for (size_t i=0;i<inside.size();i+=2) {
            string hexb = inside.substr(i,2);
            int v = hexStrToInt(hexb);
            res.push_back((uint8_t)v);
        }
        ok = true; return res;
    } else {
        // decimal numeric literal -> represent as 3 bytes (WORD)
        long long val;
        try {
            val = stoll(s);
        } catch(...) {
            return res;
        }
        res.push_back((val>>16)&0xFF);
        res.push_back((val>>8)&0xFF);
        res.push_back(val&0xFF);
        ok = true; return res;
    }
}

// helper: convert bytes to hex key string (uppercase, no prefix)
string bytesToHexKey(const vector<uint8_t> &bytes) {
    stringstream ss;
    ss << uppercase << hex;
    for (uint8_t b : bytes) ss << setw(2) << setfill('0') << (int)b;
    return ss.str();
}

// ----------------------- Expression evaluator (simple) -------------------
struct EvalResult { bool ok; uint32_t value; bool isAbsolute; string err; };
EvalResult evalExpression(const string &expr, const string &currBlock, int lineNo) {
    string s = trim(expr);
    if (s.empty()) return {false,0,false,"Empty expression"};
    vector<pair<char,string>> terms;
    size_t i = 0; char sign = '+';
    while (i < s.size()) {
        while (i<s.size() && isspace((unsigned char)s[i])) i++;
        if (i>=s.size()) break;
        if (s[i] == '+' || s[i] == '-') { sign = s[i]; i++; while (i<s.size() && isspace((unsigned char)s[i])) i++; }
        size_t j = i;
        while (j < s.size() && s[j] != '+' && s[j] != '-') j++;
        string token = trim(s.substr(i, j-i));
        if (!token.empty()) terms.push_back({sign, token});
        sign = '+';
        i = j;
    }
    long long acc = 0;
    bool first = true;
    bool accRel = false;
    for (auto &p : terms) {
        char sg = p.first; bool neg = (sg == '-'); string tok = p.second;
        uint32_t val=0; bool isSym=false;
        bool parsedNum=false;
        try {
            size_t idx=0;
            long long v = stoll(tok, &idx, 0);
            if (idx == tok.size()) { val = (uint32_t)v; parsedNum=true; }
        } catch(...) { parsedNum=false; }
        if (!parsedNum) {
            auto it = SYMTAB.find(toUpper(tok));
            if (it != SYMTAB.end()) { isSym = true; val = it->second.addr; }
            else {
                string em = "Undefined symbol '" + tok + "'";
                logError(lineNo, em);
                return {false,0,false,em};
            }
        }
        if (first && isSym) accRel = !(isSym && SYMTAB[toUpper(tok)].isAbsolute);
        long long term = val;
        if (neg) acc -= term; else acc += term;
        first = false;
    }
    return {true, (uint32_t)acc, !accRel, ""};
}

void processLiteralPool(
    vector<IntLine> &INTLINES,
    unordered_map<string, LitEntry> &LITTAB,
    uint32_t &locctr,
    string &currBlock
) {
    for (auto &p : LITTAB) {
        auto &lit = p.second;
        if (!lit.hasAddr) {
            lit.block = currBlock;
            lit.addr = locctr;
            lit.hasAddr = true;
            locctr += lit.length;

            IntLine r;
            r.lineNo = 0; // -1 대신 0으로 두거나, 별도 번호를 부여
            r.opcode = "=LITERAL";
            r.operand = lit.firstToken;   // 원래 소스 토큰 (=C'AAA' 등)
            r.block = currBlock;
            r.addr = lit.addr;
            r.raw = lit.firstToken;
            r.comment = false;
            r.generatedObject = true;

            INTLINES.push_back(r);
        }
    }
}

// ----------------------- PASS1 ------------------------------------------
void doPass1(const string &srcFile) {
    SYMTAB.clear(); LITTAB.clear(); LITERAL_TOKEN_MAP.clear(); INTLINES.clear(); BLOCKTAB.clear(); blockOrder.clear();
    programName = "";
    programStart = 0;

    BLOCKTAB[startBlockName] = Block{startBlockName, 0, 0, 0, true};
    blockOrder.push_back(startBlockName);

    vector<IntLine> parsed = parseSourceFile(srcFile);
    string currBlock = startBlockName;
    uint32_t locctr = 0;
    bool started = false;
    int lineno = 0;

    auto ensureBlock = [&](const string &bname) {
        string bn = bname.empty() ? startBlockName : bname;
        if (BLOCKTAB.find(bn) == BLOCKTAB.end()) {
            BLOCKTAB[bn] = Block{bn, 0, 0, 0, true};
            blockOrder.push_back(bn);
        }
    };

    for (auto &pline : parsed) {
        ++lineno;
        IntLine rec = pline;
        rec.block = currBlock;
        rec.addr = locctr;
        if (rec.comment) { INTLINES.push_back(rec); continue; }
        string op = trim(rec.opcode);
        string operand = trim(rec.operand);

        if (!started && op == "START") {
            started = true;
            programName = rec.label.empty() ? "      " : rec.label;
            uint32_t st = 0;
            if (!operand.empty()) {
                try { st = (uint32_t)stoul(operand,nullptr,0); } catch(...) { st = 0; }
            }
            programStart = st;
            locctr = st;
            BLOCKTAB[currBlock].locctr = locctr;
            rec.addr = locctr;
            INTLINES.push_back(rec);
            continue;
        }

        if (op == "USE") {
            string newBlock = operand.empty() ? startBlockName : operand;
            BLOCKTAB[currBlock].locctr = locctr;
            ensureBlock(newBlock);
            currBlock = newBlock;
            locctr = BLOCKTAB[currBlock].locctr;
            rec.block = currBlock; rec.addr = locctr;
            INTLINES.push_back(rec);
            continue;
        }

        if (op == "ORG") {
            uint32_t newLoc = 0; bool ok=false;
            if (operand.empty()) { ok=true; newLoc=locctr; }
            else {
                try { newLoc = (uint32_t)stoul(operand,nullptr,0); ok=true; } catch(...) { ok=false; }
                if (!ok) {
                    auto it = SYMTAB.find(toUpper(operand));
                    if (it != SYMTAB.end()) { newLoc = it->second.addr; ok=true; }
                    else {
                        auto r = evalExpression(operand, currBlock, rec.lineNo);
                        if (r.ok) { newLoc = r.value; ok=true; }
                        else logError(rec.lineNo, "ORG expr eval failed: " + operand);
                    }
                }
            }
            if (ok) { locctr = newLoc; BLOCKTAB[currBlock].locctr = locctr; }
            else logError(rec.lineNo, "ORG failed to parse operand: " + operand);
            rec.addr = locctr;
            INTLINES.push_back(rec);
            continue;
        }

        if (op == "EQU") {
            if (rec.label.empty()) {
                logError(rec.lineNo, "EQU without label");
            } else {
                if (operand.empty()) {
                    SYMTAB[toUpper(rec.label)] = SymEntry{toUpper(rec.label), locctr, currBlock, true};
                } else {
                    bool parsedNum=false; uint32_t v=0;
                    try { v = (uint32_t)stoul(operand,nullptr,0); parsedNum=true; } catch(...) { parsedNum=false; }
                    if (parsedNum) {
                        SYMTAB[toUpper(rec.label)] = SymEntry{toUpper(rec.label), v, currBlock, true};
                    } else {
                        auto it = SYMTAB.find(toUpper(operand));
                        if (it != SYMTAB.end()) {
                            SYMTAB[toUpper(rec.label)] = SymEntry{toUpper(rec.label), it->second.addr, it->second.block, it->second.isAbsolute};
                        } else {
                            auto r = evalExpression(operand, currBlock, rec.lineNo);
                            if (r.ok) SYMTAB[toUpper(rec.label)] = SymEntry{toUpper(rec.label), r.value, currBlock, r.isAbsolute};
                            else logError(rec.lineNo, "EQU expr eval failed: " + operand);
                        }
                    }
                }
            }
            INTLINES.push_back(rec);
            continue;
        }

        if (op == "LTORG") {
            processLiteralPool(INTLINES, LITTAB, locctr, currBlock);
            continue;
        }

        if (op == "END") {
            processLiteralPool(INTLINES, LITTAB, locctr, currBlock);
            INTLINES.push_back(rec); // END 자체는 남겨야 함
            break;
        }

        if (op == "BASE") { INTLINES.push_back(rec); continue; }

        // Label definition
        if (!rec.label.empty()) {
            string lab = toUpper(rec.label);
            if (SYMTAB.find(lab) != SYMTAB.end()) {
                logError(rec.lineNo, "Duplicate symbol: " + lab);
            } else {
                SYMTAB[lab] = SymEntry{lab, locctr, currBlock, false};
            }
        }

        // *** LITERAL FIX: when operand contains literal, add to LITTAB keyed by hex-value
        if (!operand.empty()) {
            string opnd = operand;
            size_t commaPos = opnd.find(',');
            if (commaPos != string::npos) opnd = trim(opnd.substr(0, commaPos));
            if (!opnd.empty() && opnd[0]=='=') {
                string litToken = opnd; // e.g., "=C'AAA'" or "=4276545"
                // compute byte sequence for the literal value (after '=')
                string litVal = litToken.substr(1);
                bool ok=false; vector<uint8_t> b = bytesFromConstant(litVal, ok);
                if (!ok) {
                    // fallback numeric -> 3 bytes
                    vector<uint8_t> arr; long long v=0;
                    try { v = stoll(litVal); } catch(...) { v=0; }
                    arr.push_back((v>>16)&0xFF); arr.push_back((v>>8)&0xFF); arr.push_back(v&0xFF);
                    b = arr; ok = true;
                }
                string hexKey = bytesToHexKey(b); // canonical key
                // if not present, insert new LitEntry in LITTAB keyed by hexKey
                if (LITTAB.find(hexKey) == LITTAB.end()) {
                    LitEntry ent;
                    ent.hexKey = hexKey;
                    ent.firstToken = litToken;
                    ent.bytes = b;
                    ent.length = (uint32_t)b.size();
                    ent.hasAddr = false;
                    ent.block = "";
                    ent.addr = 0;
                    LITTAB[hexKey] = ent;
                }
                // Map this token to the canonical hexKey (if not already mapped)
                if (LITERAL_TOKEN_MAP.find(litToken) == LITERAL_TOKEN_MAP.end()) {
                    LITERAL_TOKEN_MAP[litToken] = hexKey;
                }
            }
        }

        // Compute LOCCTR increments for opcode/directive
        uint32_t inc = 0;
        bool recognized = false;
        bool isFormat4 = false;
        string opcodeToken = op;
        if (!opcodeToken.empty() && opcodeToken[0] == '+') { isFormat4 = true; opcodeToken = opcodeToken.substr(1); }
        if (OPTAB.find(opcodeToken) != OPTAB.end()) {
            recognized = true;
            if (isFormat4) inc = 4;
            else {
                string up = opcodeToken;
                if (FORMAT1.find(up) != FORMAT1.end()) inc = 1;
                else if (FORMAT2.find(up) != FORMAT2.end()) inc = 2;
                else inc = 3;
            }
        } else {
            if (op == "WORD") { inc = 3; recognized = true; }
            else if (op == "RESW") {
                recognized = true;
                try { int n = stoi(operand); inc = 3 * n; } catch(...) { inc = 0; logError(rec.lineNo, "Invalid RESW operand"); }
            } else if (op == "RESB") {
                recognized = true;
                try { int n = stoi(operand); inc = n; } catch(...) { inc = 0; logError(rec.lineNo, "Invalid RESB operand"); }
            } else if (op == "BYTE") {
                recognized = true;
                string oper = operand;
                if (oper.size()>1 && (oper[0]=='C' || oper[0]=='c') && oper[1]=='\'' && oper.back()=='\'') {
                    string inside = oper.substr(2, oper.size()-3);
                    inc = (uint32_t)inside.size();
                } else if (oper.size()>1 && (oper[0]=='X' || oper[0]=='x') && oper[1]=='\'' && oper.back()=='\'') {
                    string inside = oper.substr(2, oper.size()-3);
                    inc = (uint32_t)(inside.size()/2);
                } else {
                    inc = 1;
                }
            } else {
                logError(rec.lineNo, "Invalid opcode or directive: " + op);
                inc = 0;
            }
        }
        // assign address THEN increment LOCCTR (so the current line's address is previous LOCCTR)
        rec.addr = locctr;
        INTLINES.push_back(rec);
        locctr += inc;
        BLOCKTAB[currBlock].locctr = locctr;
    } // end for parsed lines

    // compute block lengths
    for (auto &bn : blockOrder) BLOCKTAB[bn].length = BLOCKTAB[bn].locctr;

    // Write INTFILE and SYMTAB
    ofstream intf("INTFILE.txt");
    for (auto &r : INTLINES) {
        if (r.comment) {
            intf << setw(4) << r.lineNo << "    " << r.raw << "\n";
        } else {
            intf << setw(4) << (r.lineNo>0 ? r.lineNo : 0) << " "   // LTORG 리터럴 lineNo가 0이면 0 출력
                << setw(6) << hexPad(r.addr,6)                     // 블록 내 LOCCTR 표시
                << " [" << r.block << "] ";
            if (!r.label.empty()) intf << setw(8) << r.label << " ";
            else intf << setw(8) << " " << " ";
            intf << setw(8) << r.opcode;
            if (!r.operand.empty()) intf << " " << r.operand;
            intf << "    ; " << trim(r.raw) << "\n";
        }
    }
    intf.close();

    ofstream symf("SYMTAB.txt");
    for (auto &p : SYMTAB) {
        symf << p.first << " " << hexPad(p.second.addr,6) << " " << p.second.block << "\n";
    }
    symf.close();

    // For debug: also write LITTAB (hexKey, token, addr)
    ofstream litf("LITTAB.txt");
    for (auto &p : LITTAB) {
        litf << p.first << " " << p.second.firstToken << " len=" << p.second.length << " addr=" << (p.second.hasAddr?hexPad(p.second.addr,6):string("UNDEF")) << " block=" << p.second.block << "\n";
    }
    litf.close();

    cout << "=== PASS1 complete ===\n";
    cout << "Program start: " << hexPad(programStart,6) << " Name: " << programName << "\n";
    cout << "Blocks encountered: ";
    for (auto &bn : blockOrder) cout << bn << " ";
    cout << "\nSYMTAB entries: " << SYMTAB.size() << "\n";
    cout << "Unique LITTAB entries (by hex): " << LITTAB.size() << "\n";
    if (!ERRORS.empty()) {
        cout << "Errors/Warnings during pass1:\n";
        for (auto &e : ERRORS) cout << e << "\n";
    }
}

// ----------------------- PASS2 helpers ----------------------------------
uint32_t computeAbsoluteAddr(const string &sym, const string &currBlock, bool &ok) {
    ok = false;
    auto it = SYMTAB.find(toUpper(sym));
    if (it != SYMTAB.end()) {
        string b = it->second.block;
        uint32_t rel = it->second.addr;
        if (BLOCKTAB.find(b) != BLOCKTAB.end()) {
            ok = true;
            return BLOCKTAB[b].startAddr + rel;
        } else { ok=false; return 0; }
    }
    try { uint32_t val = (uint32_t)stoul(sym,nullptr,0); ok=true; return val; } catch(...) { ok=false; return 0; }
}
string toHexByte(uint8_t b) {
    stringstream ss; ss << uppercase << hex << setw(2) << setfill('0') << (int)b;
    return ss.str();
}
string buildFormat34(uint8_t opcode, bool n, bool i, bool x, bool b, bool p, bool e, uint32_t disp_or_addr) {
    uint8_t byte0 = (opcode & 0xFC) | ( ((n?1:0)<<1) | (i?1:0) );
    if (!e) {
        uint16_t disp12 = (uint16_t)(disp_or_addr & 0xFFF);
        uint8_t flags = ( (x?1:0)<<3 | (b?1:0)<<2 | (p?1:0)<<1 | (e?1:0) );
        uint8_t byte1 = (flags << 4) | ((disp12 >> 8) & 0x0F);
        uint8_t byte2 = disp12 & 0xFF;
        stringstream ss; ss << uppercase << hex << setw(2) << setfill('0') << (int)byte0
            << setw(2) << (int)byte1 << setw(2) << (int)byte2;
        return ss.str();
    } else {
        uint32_t addr20 = disp_or_addr & 0xFFFFF;
        uint8_t flags = ( (x?1:0)<<3 | (b?1:0)<<2 | (p?1:0)<<1 | (e?1:0) );
        uint8_t byte1 = (flags << 4) | ((addr20 >> 16) & 0x0F);
        uint8_t byte2 = (addr20 >> 8) & 0xFF;
        uint8_t byte3 = addr20 & 0xFF;
        stringstream ss; ss << uppercase << hex << setw(2) << setfill('0') << (int)byte0
            << setw(2) << (int)byte1 << setw(2) << (int)byte2 << setw(2) << (int)byte3;
        return ss.str();
    }
}
string buildFormat2(uint8_t opcode, int r1, int r2, int imm) {
    uint8_t byte0 = opcode;
    uint8_t byte1 = 0;
    if (r1 < 0) r1 = 0;
    if (r2 < 0) r2 = 0;
    byte1 = ( (r1 & 0xF) << 4 ) | ( (r2 & 0xF) & 0xF );
    if (imm >= 0) byte1 = imm & 0xFF;
    stringstream ss; ss << uppercase << hex << setw(2) << setfill('0') << (int)byte0 << setw(2) << (int)byte1;
    return ss.str();
}
string buildFormat1(uint8_t opcode) {
    stringstream ss; ss << uppercase << hex << setw(2) << setfill('0') << (int)opcode;
    return ss.str();
}

// ----------------------- PASS2 ------------------------------------------
void doPass2(const string &srcFile) {
    // assign block start addresses sequentially
    uint32_t curAddr = programStart;
    for (auto &bn : blockOrder) {
        BLOCKTAB[bn].startAddr = curAddr;
        curAddr += BLOCKTAB[bn].length;
    }
    uint32_t programLength = curAddr - programStart;

    // track base register
    uint32_t baseValue = 0; bool baseOn = false;

    // generate object code per INTLINE
    for (auto &r : INTLINES) {
        r.generatedObject = false;
        r.objectCode = "";
        if (r.comment) continue;
        string op = toUpper(r.opcode);
        string operand = trim(r.operand);

        if (op == "START" || op == "END" || op == "LTORG" || op == "USE" || op=="ORG" || op=="EQU") {
            continue;
        }
        if (op == "BASE") {
            bool ok=false; uint32_t addr=0;
            if (!operand.empty()) {
                auto it = SYMTAB.find(toUpper(operand));
                if (it != SYMTAB.end()) {
                    string b = it->second.block;
                    addr = BLOCKTAB[b].startAddr + it->second.addr; ok=true;
                } else {
                    try { addr = (uint32_t)stoul(operand,nullptr,0); ok=true; } catch(...) { ok=false; }
                }
            }
            if (ok) { baseOn = true; baseValue = addr; }
            else logError(r.lineNo, "BASE operand unresolved: " + operand);
            continue;
        }
        if (op == "WORD") {
            uint32_t val = 0;
            try { val = (uint32_t)stoul(operand,nullptr,0); } catch(...) { val = 0; }
            r.generatedObject = true; r.objectCode = hexPad(val,6);
            continue;
        }
        if (op == "BYTE") {
            string oper = operand;
            if (oper.size()>2 && (oper[0]=='C' || oper[0]=='c') && oper[1]=='\'' && oper.back()=='\'') {
                string inside = oper.substr(2, oper.size()-3);
                stringstream ss;
                for (char c : inside) ss << hex << uppercase << setw(2) << setfill('0') << (int)((unsigned char)c);
                r.generatedObject = true; r.objectCode = ss.str();
            } else if (oper.size()>2 && (oper[0]=='X' || oper[0]=='x') && oper[1]=='\'' && oper.back()=='\'') {
                string inside = oper.substr(2, oper.size()-3);
                r.generatedObject = true; r.objectCode = toUpper(inside);
            } else {
                try {
                    uint32_t v = (uint32_t)stoul(oper,nullptr,0);
                    r.generatedObject = true; r.objectCode = hexPad(v,2);
                } catch(...) { r.generatedObject=false; }
            }
            continue;
        }
        if (op == "RESW" || op == "RESB") continue;

        bool isFormat4=false;
        string opClean = op;
        if (!op.empty() && op[0] == '+') { isFormat4 = true; opClean = op.substr(1); }
        if (OPTAB.find(opClean) == OPTAB.end()) {
            logError(r.lineNo, "Undefined opcode: " + opClean);
            continue;
        }
        uint8_t opcode = OPTAB[opClean].opcode;

        if (FORMAT1.find(opClean) != FORMAT1.end()) {
            r.generatedObject = true; r.objectCode = buildFormat1(opcode); continue;
        }
        if (FORMAT2.find(opClean) != FORMAT2.end()) {
            int r1=-1, r2=0;
            string oper = operand;
            if (!oper.empty()) {
                auto parts = splitByChar(oper, ',');
                if (parts.size() >= 1) {
                    string p1 = trim(parts[0]);
                    if (REGNUM.find(toUpper(p1)) != REGNUM.end()) r1 = REGNUM[toUpper(p1)];
                    else { try { r1 = stoi(p1); } catch(...) { r1 = 0; } }
                }
                if (parts.size() >= 2) {
                    string p2 = trim(parts[1]);
                    if (REGNUM.find(toUpper(p2)) != REGNUM.end()) r2 = REGNUM[toUpper(p2)];
                    else { try { r2 = stoi(p2); } catch(...) { r2 = 0; } }
                }
            }
            r.generatedObject = true; r.objectCode = buildFormat2(opcode, r1, r2, -1);
            continue;
        }

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
                if (toUpper(after) == "X") { x = true; operNoIndex = trim(operNoIndex.substr(0, comma)); }
            }
        }
        bool operandIsLiteral = false;
        if (!operNoIndex.empty() && operNoIndex[0] == '=') operandIsLiteral = true;

        bool immediateNumeric = false; uint32_t immediateValue = 0;
        if (i && !operNoIndex.empty() && isdigit((unsigned char)operNoIndex[0])) {
            try { immediateValue = (uint32_t)stoul(operNoIndex,nullptr,0); immediateNumeric=true; } catch(...) { immediateNumeric=false; }
        }

        uint32_t instrAddrAbs = BLOCKTAB[r.block].startAddr + r.addr;
        int instrLen = isFormat4 ? 4 : 3;
        if (isFormat4) e = true; else e = false;

        if (immediateNumeric && !operandIsLiteral) {
            if (!isFormat4 && immediateValue > 0xFFF) { e = true; isFormat4 = true; instrLen = 4; }
            r.generatedObject = true; r.objectCode = buildFormat34(opcode, n, i, x, false, false, e, immediateValue);
            continue;
        }

        // *** LITERAL FIX: when resolving literal operand, map token -> hexKey -> LITTAB entry
        uint32_t targetAbs = 0; bool okTarget=false;
        if (operandIsLiteral) {
            string litTok = operNoIndex; // includes '='
            // find hexKey for this token
            auto mapIt = LITERAL_TOKEN_MAP.find(litTok);
            if (mapIt != LITERAL_TOKEN_MAP.end()) {
                string hexKey = mapIt->second;
                auto litIt = LITTAB.find(hexKey);
                if (litIt != LITTAB.end() && litIt->second.hasAddr) {
                    targetAbs = BLOCKTAB[litIt->second.block].startAddr + litIt->second.addr;
                    okTarget = true;
                } else {
                    logError(r.lineNo, string("Literal '") + litTok + "' not placed yet or unknown");
                    okTarget = false;
                }
            } else {
                logError(r.lineNo, string("Literal token '") + litTok + "' unknown in LITERAL_TOKEN_MAP");
                okTarget = false;
            }
        } else if (!operNoIndex.empty()) {
            auto it = SYMTAB.find(toUpper(operNoIndex));
            if (it != SYMTAB.end()) {
                targetAbs = BLOCKTAB[it->second.block].startAddr + it->second.addr; okTarget=true;
            } else {
                try { targetAbs = (uint32_t)stoul(operNoIndex,nullptr,0); okTarget=true; } catch(...) { okTarget=false; }
            }
        } else {
            okTarget = false;
        }

        if (!okTarget && !(opClean == "RSUB")) {
            logError(r.lineNo, "Undefined operand or symbol: " + operNoIndex);
            continue;
        }

        if (opClean == "RSUB") {
            r.generatedObject = true;
            r.objectCode = buildFormat34(opcode, true, true, false, false, false, false, 0);
            continue;
        }

        if (!isFormat4) {
            int32_t disp = (int32_t)targetAbs - (int32_t)(instrAddrAbs + instrLen);
            if (disp >= -2048 && disp <= 2047) {
                p = true; b = false;
                uint32_t disp12 = (uint32_t)(disp & 0xFFF);
                r.generatedObject = true; r.objectCode = buildFormat34(opcode, n, i, x, b, p, false, disp12);
                continue;
            } else {
                if (baseOn) {
                    int32_t dispb = (int32_t)targetAbs - (int32_t)baseValue;
                    if (dispb >= 0 && dispb <= 4095) {
                        b = true; p = false;
                        uint32_t disp12 = (uint32_t)(dispb & 0xFFF);
                        r.generatedObject = true; r.objectCode = buildFormat34(opcode, n, i, x, b, p, false, disp12);
                        continue;
                    } else {
                        isFormat4 = true; e = true; instrLen = 4;
                    }
                } else {
                    isFormat4 = true; e = true; instrLen = 4;
                }
            }
        }
        if (isFormat4) {
            r.generatedObject = true;
            r.objectCode = buildFormat34(opcode, n, i, x, false, false, true, targetAbs);
            continue;
        }
    } // end generate per INTLINE

    // Build literal pseudo fragments (only for unique hexKey entries that have addresses)
    struct LiteralPseudo { uint32_t absAddr; string block; string bytesHex; uint32_t length; string hexKey; string token; };
    vector<LiteralPseudo> litLines;
    // preserve original insertion order: iterate LITERAL_TOKEN_MAP in insertion order would be nondeterministic in unordered_map,
    // but we can collect unique hexKeys then sort by assigned addr to ensure order in memory.
    for (auto &p : LITTAB) {
        if (p.second.hasAddr) {
            uint32_t abs = BLOCKTAB[p.second.block].startAddr + p.second.addr;
            stringstream ss;
            for (auto b : p.second.bytes) ss << hex << uppercase << setw(2) << setfill('0') << (int)b;
            litLines.push_back({abs, p.second.block, ss.str(), p.second.length, p.first, p.second.firstToken});
        }
    }
    sort(litLines.begin(), litLines.end(), [](const LiteralPseudo &a, const LiteralPseudo &b){ return a.absAddr < b.absAddr; });

    // collect object fragments from INTLINES
    struct ObjFrag { uint32_t absAddr; string block; string bytes; };
    vector<ObjFrag> frags;
    for (auto &r : INTLINES) {
        if (r.generatedObject) {
            uint32_t abs = BLOCKTAB[r.block].startAddr + r.addr;
            frags.push_back({abs, r.block, r.objectCode});
        }
    }
    for (auto &l : litLines) frags.push_back({l.absAddr, l.block, l.bytesHex});
    sort(frags.begin(), frags.end(), [](const ObjFrag &a, const ObjFrag &b){
        if (a.block != b.block) return a.block < b.block;
        return a.absAddr < b.absAddr;
    });

    // M records for format4
    vector<pair<uint32_t,int>> MRECS;
    for (auto &r : INTLINES) {
        if (r.generatedObject) {
            if (r.objectCode.size() == 8) {
                uint32_t abs = BLOCKTAB[r.block].startAddr + r.addr;
                MRECS.push_back({abs+1, 5});
            }
        }
    }

    // Write OBJFILE
    ofstream objf("OBJFILE.obj");
    string pname = programName;
    if (pname.size() > 6) pname = pname.substr(0,6);
    else pname = pname + string(6 - pname.size(), ' ');
    objf << "H" << pname << hexPad(programStart,6) << hexPad(programLength,6) << "\n";

    // Per-block T records
    for (auto &bn : blockOrder) {
        vector<ObjFrag> bfrags;
        for (auto &f : frags) if (f.block == bn) bfrags.push_back(f);
        if (bfrags.empty()) continue;
        sort(bfrags.begin(), bfrags.end(), [](const ObjFrag &a, const ObjFrag &b){ return a.absAddr < b.absAddr; });
        size_t idx = 0;
        while (idx < bfrags.size()) {
            uint32_t tStart = bfrags[idx].absAddr;
            string tBytes = "";
            uint32_t tLenBytes = 0;
            uint32_t expectedNext = tStart;
            while (idx < bfrags.size()) {
                auto &f = bfrags[idx];
                uint32_t fbyteLen = (uint32_t)(f.bytes.size() / 2);
                if (f.absAddr != expectedNext) break;
                if (tLenBytes + fbyteLen > 30) break;
                tBytes += f.bytes;
                tLenBytes += fbyteLen;
                expectedNext += fbyteLen;
                ++idx;
            }
            if (tLenBytes == 0) {
                auto &f = bfrags[idx++];
                uint32_t fbyteLen = (uint32_t)(f.bytes.size()/2);
                objf << "T" << hexPad(f.absAddr,6) << hexPad(fbyteLen,2) << f.bytes << "\n";
            } else {
                objf << "T" << hexPad(tStart,6) << hexPad(tLenBytes,2) << tBytes << "\n";
            }
        }
    }

    for (auto &m : MRECS) {
        objf << "M" << hexPad(m.first,6) << setw(2) << setfill('0') << uppercase << hex << m.second << "\n";
    }

    objf << "E" << hexPad(programStart,6) << "\n";
    objf.close();

    cout << "=== PASS2 complete ===\n";
    cout << "Program length: " << hexPad(programLength,6) << "\n";
    cout << "Wrote OBJFILE.obj, SYMTAB.txt, INTFILE.txt, LITTAB.txt\n";
    if (!ERRORS.empty()) {
        cout << "Errors/Warnings during pass2:\n";
        for (auto &e : ERRORS) cout << e << "\n";
    }
}

// ----------------------- MAIN -------------------------------------------
int main(int argc, char** argv) {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    cout << "SIC/XE 2-pass assembler (fixed literal handling)\n";
    string src;
    if (argc >= 2) {
        src = argv[1];
    } else {
        cout << "Enter source filename to assemble: ";
        if (!std::getline(cin, src)) { cerr << "No input received. Exiting.\n"; return 1; }
        src = trim(src);
        if (src.empty()) { cerr << "Empty filename provided. Exiting.\n"; return 1; }
    }

    string optabFile = "optab.txt";
    if (!loadOptab(optabFile)) {
        cerr << "Failed to load OPTAB from " << optabFile << ". Make sure the file exists and contains entries.\n";
        return 2;
    }
    cout << "OPTAB loaded: " << OPTAB.size() << " entries.\n";

    BLOCKTAB.clear(); blockOrder.clear();
    BLOCKTAB["DEFAULT"] = Block{"DEFAULT",0,0,0,true};
    blockOrder.push_back("DEFAULT");

    doPass1(src);
    doPass2(src);

    cout << "Assembly finished. Check files: SYMTAB.txt, INTFILE.txt, LITTAB.txt, OBJFILE.obj\n";
    return 0;
}
