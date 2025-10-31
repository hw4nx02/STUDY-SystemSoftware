#include <bits/stdc++.h>
using namespace std;

// --- Utility functions ---------------------------------------------------
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
    int v; stringstream ss;
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

// --- OPTAB loader -------------------------------------------------------
struct OptEntry {
    string mnemonic;
    uint8_t opcode; // 8-bit opcode
    // formats not stored in file; we'll infer common format1/2 lists
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

// --- Registers map for format2 -----------------------------------------
unordered_map<string,int> REGNUM = {
    {"A",0},{"X",1},{"L",2},{"B",3},{"S",4},{"T",5},{"F",6},{"PC",8},{"SW",9}
};

// --- Format classification (approx) -------------------------------------
// Some mnemonics are format1 or format2; otherwise default format3/4 supported by '+'
unordered_set<string> FORMAT1 = {"FIX","FLOAT","NORM","SIO","HIO","TIO"};
unordered_set<string> FORMAT2 = {
    "ADDR","COMPR","CLEAR","TIXR","RMO","SVC","SHIFTL","SHIFTR","MULR","DIVR","SUBR"
};

// --- Data structures: SYMTAB, LITTAB, BLOCKTAB, Intermediate lines -------
struct SymEntry {
    string name;
    uint32_t addr;   // relative addr within block
    string block;    // block name
    bool isAbsolute; // true if EQU with absolute
};
map<string, SymEntry> SYMTAB;

struct LitEntry {
    string name; // literal token, e.g., =C'AAA' or =X'41'
    vector<uint8_t> bytes;
    uint32_t length;
    bool hasAddr;
    string block;
    uint32_t addr;
};
map<string, LitEntry> LITTAB;

struct Block {
    string name;
    uint32_t locctr;
    uint32_t length;
    uint32_t startAddr; // assigned later in pass2
    bool used;
};
vector<string> blockOrder; // order encountered
unordered_map<string, Block> BLOCKTAB;

// IntermediateRecord: info that pass2 will use
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

// Globals for pass1
uint32_t programStart = 0;
string programName;
string startBlockName = "DEFAULT";

// Error logging
vector<string> ERRORS;
void logError(int lineNo, const string &msg) {
    stringstream ss; ss << "Line " << lineNo << ": " << msg;
    ERRORS.push_back(ss.str());
}

// --- Parsing source lines (flexible format) ------------------------------
vector<IntLine> parseSourceFile(const string &fname) {
    vector<IntLine> out;
    ifstream ifs(fname);
    if (!ifs) { cerr << "Cannot open source file: " << fname << "\n"; return out; }
    string raw;
    int lineno = 0;
    while (getline(ifs, raw)) {
        ++lineno;
        string line = raw;
        if (!line.empty() && line.back()=='\r') line.pop_back(); // windows cr
        IntLine rec; rec.lineNo = lineno; rec.raw = line; rec.comment = false;
        string t = trim(line);
        if (t.empty()) { rec.comment = true; out.push_back(rec); continue; }
        // comment lines starting with '.'
        size_t firstNS = line.find_first_not_of(" \t");
        if (firstNS != string::npos && line[firstNS] == '.') { rec.comment = true; out.push_back(rec); continue; }
        // Determine label vs no-label: if first char not whitespace -> label
        bool hasLabel = !(line.size() > 0 && (line[0]==' '||line[0]=='\t'));
        string working = line;
        if (hasLabel) {
            // label is first token (up to whitespace)
            istringstream iss(line);
            string lab; iss >> lab;
            rec.label = trim(lab);
            // remainder after label
            size_t pos = line.find(lab);
            if (pos!=string::npos) {
                string rem = line.substr(pos + lab.size());
                rem = trim(rem);
                if (rem.empty()) { rec.opcode = ""; rec.operand = ""; out.push_back(rec); continue; }
                // get opcode token
                istringstream iss2(rem); string op; iss2 >> op;
                rec.opcode = trim(op);
                size_t oppos = rem.find(op);
                if (oppos!=string::npos) {
                    string oper = rem.substr(oppos + op.size());
                    rec.operand = trim(oper);
                }
            }
        } else {
            // no label: first token is opcode
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
        // uppercase opcode
        rec.opcode = toUpper(rec.opcode);
        out.push_back(rec);
    }
    return out;
}

// --- Helpers for literals parsing ---------------------------------------
vector<uint8_t> bytesFromConstant(const string &operand, bool &ok) {
    // operand is like C'ABC' or X'4141' or numeric literal like 4276545
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
        // each two hex digits -> one byte
        if (inside.size() % 2 != 0) return res;
        for (size_t i=0;i<inside.size();i+=2) {
            string hexb = inside.substr(i,2);
            int v = hexStrToInt(hexb);
            res.push_back((uint8_t)v);
        }
        ok = true; return res;
    } else {
        // numeric decimal constant : produce its binary representation in 3 bytes (WORD-like?) 
        // But for literal like =4276545 treat as decimal integer and store as 3 or 4 bytes heuristically.
        // We'll represent decimal as 3 bytes (WORD).
        long long val;
        try {
            val = stoll(s);
        } catch(...) {
            return res;
        }
        // store into minimal bytes (here 3 bytes as WORD)
        res.push_back((val>>16)&0xFF);
        res.push_back((val>>8)&0xFF);
        res.push_back(val&0xFF);
        ok = true; return res;
    }
}

// --- Expression evaluator for EQU/ORG (supports + and - with symbols and numbers) ---
struct EvalResult {
    bool ok;
    uint32_t value;
    bool isAbsolute;
    string err;
};
EvalResult evalExpression(const string &expr, const string &currBlock, int lineNo) {
    // Tokenize by + and - with order left to right (no parens, no precedence beyond left-to-right)
    // Support tokens: decimal number, hex 0x.., symbol names (must be in SYMTAB or numeric defined blocks)
    string s = expr;
    // Replace '-' with '+-' trick by splitting but careful about leading '-'
    vector<pair<char,string>> terms; // sign, token
    size_t i = 0;
    char sign = '+';
    while (i < s.size()) {
        // skip spaces
        while (i<s.size() && isspace((unsigned char)s[i])) i++;
        if (i>=s.size()) break;
        if (s[i] == '+' || s[i] == '-') { sign = s[i]; i++; while (i<s.size() && isspace((unsigned char)s[i])) i++; }
        // read token until next +/-
        size_t j = i;
        while (j < s.size() && s[j] != '+' && s[j] != '-') j++;
        string token = trim(s.substr(i, j-i));
        if (!token.empty()) terms.push_back({sign, token});
        sign = '+'; // reset default
        i = j;
    }
    long long acc = 0;
    bool accAbs = true; // start as absolute unless symbol relative encountered?
    bool first = true;
    for (auto &p : terms) {
        char sg = p.first;
        string tok = p.second;
        bool neg = (sg == '-');
        // token could be numeric or symbol
        uint32_t val=0; bool isSym=false;
        // detect hex like X'..' or 0x..? But for simplicity support decimal numbers and symbols.
        bool parsedNum=false;
        // decimal?
        try {
            size_t idx=0;
            long long v = stoll(tok, &idx, 0); // base 0 allows 0x.. as hex
            if (idx == tok.size()) { val = (uint32_t)v; parsedNum=true; }
        } catch(...) { parsedNum=false; }
        if (!parsedNum) {
            // maybe token is SYMBOL (in SYMTAB)
            auto it = SYMTAB.find(toUpper(tok));
            if (it != SYMTAB.end()) { isSym = true; val = it->second.addr; }
            else {
                // maybe it's an arithmetic expr involving BLOCK names like BUFFERA+4096?
                // try to parse token containing '+' or '-': recursive call is complex; skip for now
                logError(lineNo, "Undefined symbol '" + tok + "' in expression '" + expr + "'");
                return {false,0,false,"Undefined symbol " + tok};
            }
        }
        if (first && !isSym) accAbs = true;
        if (isSym) accAbs = accAbs; // placeholder

        long long term = val;
        if (neg) acc -= term; else acc += term;
        first = false;
    }
    return {true, (uint32_t)acc, accAbs, ""};
}

// --- Pass1 implementation ------------------------------------------------
void doPass1(const string &srcFile) {
    // initialize
    SYMTAB.clear();
    LITTAB.clear();
    INTLINES.clear();
    BLOCKTAB.clear();
    blockOrder.clear();
    programName = "";
    programStart = 0;
    // default block
    BLOCKTAB[startBlockName] = Block{startBlockName, 0, 0, 0, true};
    blockOrder.push_back(startBlockName);

    // read lines
    vector<IntLine> parsed = parseSourceFile(srcFile);
    // we'll iterate, maintain LOCCTR per current block
    string currBlock = startBlockName;
    uint32_t locctr = 0;
    bool started = false;
    uint32_t programLen = 0;
    int lineno = 0;

    // helper to ensure block exists
    auto ensureBlock = [&](const string &bname) {
        string bn = bname.empty() ? startBlockName : bname;
        if (BLOCKTAB.find(bn) == BLOCKTAB.end()) {
            BLOCKTAB[bn] = Block{bn, 0, 0, 0, true};
            blockOrder.push_back(bn);
        }
    };

    // helper to process literal placement (LTORG/END)
    auto processLiteralsAt = [&](int lineNo) {
        // for each literal in LITTAB that has hasAddr==false, assign address at current block locctr
        for (auto &p : LITTAB) {
            if (!p.second.hasAddr && p.second.block.empty()) {
                // assign to current block
                p.second.block = currBlock;
                p.second.addr = locctr;
                p.second.hasAddr = true;
                locctr += p.second.length;
            }
        }
    };

    for (auto &pline : parsed) {
        ++lineno;
        IntLine rec = pline;
        rec.block = currBlock;
        rec.addr = locctr;
        if (rec.comment) {
            INTLINES.push_back(rec);
            continue;
        }
        string op = trim(rec.opcode);
        string operand = trim(rec.operand);
        // START directive handling
        if (!started && op == "START") {
            started = true;
            programName = rec.label.empty() ? "      " : rec.label;
            // parse operand as start address (decimal or hex)
            uint32_t st = 0;
            if (!operand.empty()) {
                try {
                    st = (uint32_t)stoul(operand, nullptr, 0);
                } catch(...) { st = 0; }
            }
            programStart = st;
            locctr = st;
            // set default block locctr as start value
            BLOCKTAB[currBlock].locctr = locctr;
            rec.addr = locctr;
            INTLINES.push_back(rec);
            continue;
        }

        // USE directive: switch block
        if (op == "USE") {
            string newBlock = operand.empty() ? startBlockName : operand;
            // save current block locctr
            BLOCKTAB[currBlock].locctr = locctr;
            // ensure new block exists
            ensureBlock(newBlock);
            currBlock = newBlock;
            locctr = BLOCKTAB[currBlock].locctr; // restore
            rec.block = currBlock; rec.addr = locctr;
            INTLINES.push_back(rec);
            continue;
        }

        // ORG directive: set LOCCTR (supports numeric or symbol expressions)
        if (op == "ORG") {
            // evaluate operand
            uint32_t newLoc = 0;
            bool ok = false;
            if (operand.empty()) {
                newLoc = BLOCKTAB[currBlock].locctr; // ??? if empty, revert to previous? we'll keep current
            } else {
                // Try numeric
                try {
                    newLoc = (uint32_t)stoul(operand, nullptr, 0);
                    ok = true;
                } catch(...) { ok = false; }
                if (!ok) {
                    // try symbol in SYMTAB
                    auto it = SYMTAB.find(toUpper(operand));
                    if (it != SYMTAB.end()) { newLoc = it->second.addr; ok = true; }
                    else {
                        // try expression evaluation (basic)
                        auto r = evalExpression(operand, currBlock, rec.lineNo);
                        if (r.ok) { newLoc = r.value; ok = true; }
                        else logError(rec.lineNo, "ORG expr eval failed: " + operand);
                    }
                }
            }
            if (ok) {
                locctr = newLoc;
                BLOCKTAB[currBlock].locctr = locctr;
            } else {
                logError(rec.lineNo, "ORG failed to parse operand: " + operand);
            }
            rec.addr = locctr;
            INTLINES.push_back(rec);
            continue;
        }

        // EQU directive: assign symbol value
        if (op == "EQU") {
            if (rec.label.empty()) {
                logError(rec.lineNo, "EQU without label");
            } else {
                // evaluate operand
                if (operand.empty()) {
                    // treat as 0?
                    SYMTAB[toUpper(rec.label)] = SymEntry{toUpper(rec.label), 0, currBlock, true};
                } else {
                    // support constant or symbol or expr
                    // try number
                    bool parsedNum=false;
                    uint32_t v=0;
                    try {
                        v = (uint32_t)stoul(operand, nullptr, 0);
                        parsedNum=true;
                    } catch(...) { parsedNum=false; }
                    if (parsedNum) {
                        SYMTAB[toUpper(rec.label)] = SymEntry{toUpper(rec.label), v, currBlock, true};
                    } else {
                        // try existing symbol
                        string key = toUpper(operand);
                        auto it = SYMTAB.find(key);
                        if (it != SYMTAB.end()) {
                            SYMTAB[toUpper(rec.label)] = SymEntry{toUpper(rec.label), it->second.addr, it->second.block, it->second.isAbsolute};
                        } else {
                            // try basic expr
                            auto r = evalExpression(operand, currBlock, rec.lineNo);
                            if (r.ok) {
                                SYMTAB[toUpper(rec.label)] = SymEntry{toUpper(rec.label), r.value, currBlock, r.isAbsolute};
                            } else {
                                logError(rec.lineNo, "EQU expr eval failed: " + operand);
                            }
                        }
                    }
                }
            }
            INTLINES.push_back(rec);
            continue;
        }

        // LTORG: place pending literals here
        if (op == "LTORG") {
            // For every literal in LITTAB without addr, assign current locctr
            for (auto &p : LITTAB) {
                if (!p.second.hasAddr) {
                    p.second.block = currBlock;
                    p.second.addr = locctr;
                    p.second.hasAddr = true;
                    locctr += p.second.length;
                }
            }
            BLOCKTAB[currBlock].locctr = locctr;
            rec.addr = locctr;
            INTLINES.push_back(rec);
            continue;
        }

        // END: finalize (place literals)
        if (op == "END") {
            // place all literals not placed
            for (auto &p : LITTAB) {
                if (!p.second.hasAddr) {
                    p.second.block = currBlock;
                    p.second.addr = locctr;
                    p.second.hasAddr = true;
                    locctr += p.second.length;
                }
            }
            BLOCKTAB[currBlock].locctr = locctr;
            // push END record
            INTLINES.push_back(rec);
            // done scanning
            break;
        }

        // BASE directive: handled in pass2; just record
        if (op == "BASE") { INTLINES.push_back(rec); continue; }

        // Handle label: define in SYMTAB (if present)
        if (!rec.label.empty()) {
            string lab = toUpper(rec.label);
            if (SYMTAB.find(lab) != SYMTAB.end()) {
                logError(rec.lineNo, "Duplicate symbol: " + lab);
            } else {
                SYMTAB[lab] = SymEntry{lab, locctr, currBlock, false};
            }
        }

        // If operand contains literal (starts with '=') add to LITTAB if not already
        if (!operand.empty()) {
            string opnd = operand;
            // remove ,X indexing for literal recognition
            size_t commaPos = opnd.find(',');
            if (commaPos != string::npos) opnd = trim(opnd.substr(0, commaPos));
            if (!opnd.empty() && opnd[0]=='=') {
                string lit = opnd;
                if (LITTAB.find(lit) == LITTAB.end()) {
                    // parse literal bytes
                    string litVal = lit.substr(1); // after '='
                    bool ok=false; vector<uint8_t> b = bytesFromConstant(litVal, ok);
                    if (!ok) {
                        // it's numeric: try parse decimal
                        vector<uint8_t> arr; long long v=0;
                        try { v = stoll(litVal); } catch(...) { v=0; }
                        arr.push_back((v>>16)&0xFF); arr.push_back((v>>8)&0xFF); arr.push_back(v&0xFF);
                        LITTAB[lit] = LitEntry{lit, arr, (uint32_t)arr.size(), false, "", 0};
                    } else {
                        LITTAB[lit] = LitEntry{lit, b, (uint32_t)b.size(), false, "", 0};
                    }
                }
            }
        }

        // Compute LOCCTR increments based on opcode/directive
        uint32_t inc = 0;
        bool recognized = false;
        // check for + (format4)
        bool isFormat4 = false;
        string opcodeToken = op;
        if (!opcodeToken.empty() && opcodeToken[0] == '+') { isFormat4 = true; opcodeToken = opcodeToken.substr(1); }
        // if opcode token is in OPTAB?
        if (OPTAB.find(opcodeToken) != OPTAB.end()) {
            recognized = true;
            // determine format
            if (isFormat4) inc = 4;
            else {
                string up = opcodeToken;
                if (FORMAT1.find(up) != FORMAT1.end()) inc = 1;
                else if (FORMAT2.find(up) != FORMAT2.end()) inc = 2;
                else inc = 3; // default format3
            }
        } else {
            // directives
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
                    // numeric constant? treat as WORD
                    inc = 1; // minimal
                }
            } else {
                // unrecognized opcode — still store and increment by 3 as guess? better: error
                logError(rec.lineNo, "Invalid opcode or directive: " + op);
                inc = 0;
            }
        }
        // assign address then increment locctr
        rec.addr = locctr;
        INTLINES.push_back(rec);
        locctr += inc;
        // update block locctr
        BLOCKTAB[currBlock].locctr = locctr;
    } // end for lines

    // After scanning all lines, compute block lengths
    for (auto &bn : blockOrder) {
        BLOCKTAB[bn].length = BLOCKTAB[bn].locctr; // because locctr was relative start at 0 per block
    }

    // Write INTFILE and SYMTAB
    ofstream intf("INTFILE.txt");
    for (auto &r : INTLINES) {
        if (r.comment) {
            intf << setw(4) << r.lineNo << "    " << r.raw << "\n";
        } else {
            intf << setw(4) << r.lineNo << " " << setw(6) << hexPad(r.addr,6) << " [" << r.block << "] ";
            if (!r.label.empty()) intf << setw(8) << r.label << " ";
            else intf << setw(8) << " " << " ";
            intf << setw(8) << r.opcode << " " << r.operand << "    ; " << r.raw << "\n";
        }
    }
    intf.close();

    ofstream symf("SYMTAB.txt");
    for (auto &p : SYMTAB) {
        symf << p.first << " " << hexPad(p.second.addr,6) << " " << p.second.block << "\n";
    }
    symf.close();

    // Print summary to console
    cout << "=== PASS1 complete ===\n";
    cout << "Program start: " << hexPad(programStart,6) << " Name: " << programName << "\n";
    cout << "Blocks encountered: ";
    for (auto &bn : blockOrder) cout << bn << " ";
    cout << "\nSYMTAB entries: " << SYMTAB.size() << "\n";
    cout << "LITTAB entries: " << LITTAB.size() << "\n";
    if (!ERRORS.empty()) {
        cout << "Errors found in pass1:\n";
        for (auto &e : ERRORS) cout << e << "\n";
    }
}

// --- Helpers for pass2 (address resolution, object generation) ----------
uint32_t computeAbsoluteAddr(const string &sym, const string &currBlock, bool &ok) {
    ok = false;
    // symbol may be in SYMTAB
    auto it = SYMTAB.find(toUpper(sym));
    if (it != SYMTAB.end()) {
        // absolute = blockStart + relative
        string b = it->second.block;
        uint32_t rel = it->second.addr;
        // block start assigned later; but for now use BLOCKTAB[b].startAddr
        if (BLOCKTAB.find(b) != BLOCKTAB.end()) {
            ok = true;
            return BLOCKTAB[b].startAddr + rel;
        } else {
            ok = false; return 0;
        }
    }
    // maybe numeric
    try {
        uint32_t val = (uint32_t)stoul(sym, nullptr, 0);
        ok = true; return val;
    } catch(...) { ok=false; return 0; }
}

// encode helper: format3 3bytes or format4 4bytes
string toHexByte(uint8_t b) {
    stringstream ss; ss << uppercase << hex << setw(2) << setfill('0') << (int)b;
    return ss.str();
}

// build object code for format3/4
string buildFormat34(uint8_t opcode, bool n, bool i, bool x, bool b, bool p, bool e, uint32_t disp_or_addr) {
    // opcode: 8-bit value; low 2 bits will be set to n,i
    uint8_t byte0 = (opcode & 0xFC) | ( (n?1:0)<<1 | (i?1:0) );
    if (!e) {
        // format3: 3 bytes
        uint16_t disp12 = (uint16_t)(disp_or_addr & 0xFFF);
        uint8_t flags = ( (x?1:0)<<3 | (b?1:0)<<2 | (p?1:0)<<1 | (e?1:0) );
        uint8_t byte1 = (flags << 4) | ((disp12 >> 8) & 0x0F);
        uint8_t byte2 = disp12 & 0xFF;
        stringstream ss; ss << uppercase << hex << setw(2) << setfill('0') << (int)byte0
            << setw(2) << (int)byte1 << setw(2) << (int)byte2;
        return ss.str();
    } else {
        // format4: 4 bytes (e==1)
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

// build format2 object code (2 bytes)
string buildFormat2(uint8_t opcode, int r1, int r2, int imm) {
    // some format2 instructions have only one register or a register and a number
    uint8_t byte0 = opcode;
    uint8_t byte1 = 0;
    if (r1 < 0) r1 = 0;
    if (r2 < 0) r2 = 0;
    byte1 = ( (r1 & 0xF) << 4 ) | ( (r2 & 0xF) & 0xF );
    if (imm >= 0) byte1 = imm & 0xFF;
    stringstream ss; ss << uppercase << hex << setw(2) << setfill('0') << (int)byte0 << setw(2) << (int)byte1;
    return ss.str();
}

// format1: single byte simple opcode
string buildFormat1(uint8_t opcode) {
    stringstream ss; ss << uppercase << hex << setw(2) << setfill('0') << (int)opcode;
    return ss.str();
}

// --- Pass2 ---------------------------------------------------------------
void doPass2(const string &srcFile) {
    // First: assign start addresses for blocks sequentially starting at programStart
    uint32_t curAddr = programStart;
    for (auto &bn : blockOrder) {
        BLOCKTAB[bn].startAddr = curAddr;
        curAddr += BLOCKTAB[bn].length;
    }
    uint32_t programLength = curAddr - programStart;

    // We'll iterate INTLINES and generate object codes
    // Keep track of BASE register if any
    uint32_t baseValue = 0; bool baseOn = false;

    // Build map from lineNo->index in INTLINES to find in vector
    // (We have INTLINES global from pass1)
    // (However in this combined implementation we used INTLINES in pass1; ensure it exists)
    // For safety, if INTLINES empty, read INTFILE
    if (INTLINES.empty()) {
        // attempt to read INTFILE.txt to reconstruct INTLINES (basic)
        // But in our flow INTLINES was filled in pass1 so skip
    }

    // go through intermediate lines and produce object code strings
    for (auto &r : INTLINES) {
        r.generatedObject = false;
        r.objectCode = "";
        if (r.comment) continue;
        string op = toUpper(r.opcode);
        string operand = trim(r.operand);
        // directives handling in pass2
        if (op == "START" || op == "END" || op == "LTORG" || op == "USE" || op=="ORG" || op=="EQU") {
            // no object code
            continue;
        }
        if (op == "BASE") {
            // set base to operand value's absolute address
            bool ok=false;
            uint32_t addr=0;
            // operand might be symbol
            if (!operand.empty()) {
                auto it = SYMTAB.find(toUpper(operand));
                if (it != SYMTAB.end()) {
                    string b = it->second.block;
                    addr = BLOCKTAB[b].startAddr + it->second.addr; ok = true;
                } else {
                    // numeric?
                    try { addr = (uint32_t)stoul(operand,nullptr,0); ok = true; } catch(...) { ok=false; }
                }
            }
            if (ok) { baseOn = true; baseValue = addr; }
            else { logError(r.lineNo, "BASE operand unresolved: " + operand); }
            continue;
        }
        if (op == "WORD") {
            // 3-byte constant (decimal)
            uint32_t val = 0;
            try { val = (uint32_t)stoul(operand, nullptr, 0); } catch(...) { val = 0; }
            string oc = hexPad(val,6);
            r.generatedObject = true;
            r.objectCode = oc;
            continue;
        }
        if (op == "BYTE") {
            // C'...' or X'...'
            string oper = operand;
            if (oper.size()>2 && (oper[0]=='C' || oper[0]=='c') && oper[1]=='\'' && oper.back()=='\'') {
                string inside = oper.substr(2, oper.size()-3);
                stringstream ss;
                for (char c : inside) { ss << hex << uppercase << setw(2) << setfill('0') << (int)((unsigned char)c); }
                r.generatedObject = true;
                r.objectCode = ss.str();
            } else if (oper.size()>2 && (oper[0]=='X' || oper[0]=='x') && oper[1]=='\'' && oper.back()=='\'') {
                string inside = oper.substr(2, oper.size()-3);
                r.generatedObject = true;
                r.objectCode = toUpper(inside);
            } else {
                // numeric -> WORD-like or single byte
                try {
                    uint32_t v = (uint32_t)stoul(oper,nullptr,0);
                    r.generatedObject = true;
                    r.objectCode = hexPad(v,2);
                } catch(...) { r.generatedObject=false; }
            }
            continue;
        }
        if (op == "RESW" || op == "RESB") {
            // no object code
            continue;
        }

        // check for literal-only lines: in pass1 literals were assigned addresses via LTORG/END,
        // but we stored them in LITTAB, not as separate INTLINES. For simplicity, when encountering
        // a line in INTLINES that matches a literal definition (label=="" and opcode=="="?) we skip.
        // We'll need to also emit literal data into T records by scanning LITTAB and emitting as separate pseudo-lines.
        // For simplicity, here we only handle normal instructions.

        // Opcodes in OPTAB?
        // handle plus for format4
        bool isFormat4=false;
        string opClean = op;
        if (!op.empty() && op[0] == '+') { isFormat4 = true; opClean = op.substr(1); }
        if (OPTAB.find(opClean) == OPTAB.end()) {
            // maybe it's a directive or undefined mnemonic
            // allow J (jump) which is in optab file as J 3C
            // if not found, log error and continue
            logError(r.lineNo, "Undefined opcode: " + opClean);
            continue;
        }
        uint8_t opcode = OPTAB[opClean].opcode;
        // classify format
        if (FORMAT1.find(opClean) != FORMAT1.end()) {
            // 1-byte
            r.generatedObject = true;
            r.objectCode = buildFormat1(opcode);
            continue;
        }
        if (FORMAT2.find(opClean) != FORMAT2.end()) {
            // format2: operand examples: "A, S" or "A" or "S,1"
            int r1=-1, r2=0;
            string oper = operand;
            if (!oper.empty()) {
                auto parts = splitByChar(oper, ',');
                if (parts.size() >= 1) {
                    string p1 = trim(parts[0]);
                    if (REGNUM.find(toUpper(p1)) != REGNUM.end()) r1 = REGNUM[toUpper(p1)];
                    else {
                        // maybe numeric
                        try { r1 = stoi(p1); } catch(...) { r1 = 0; }
                    }
                }
                if (parts.size() >= 2) {
                    string p2 = trim(parts[1]);
                    if (REGNUM.find(toUpper(p2)) != REGNUM.end()) r2 = REGNUM[toUpper(p2)];
                    else { try { r2 = stoi(p2); } catch(...) { r2 = 0; } }
                }
            }
            r.generatedObject = true;
            r.objectCode = buildFormat2(opcode, r1, r2, -1);
            continue;
        }

        // Format 3 or 4
        bool n=false,i=false,x=false,b=false,p=false,e=false;
        // addressing mode: immediate (#) indirect (@), simple (none)
        string oper = operand;
        if (!oper.empty() && oper[0] == '#') { i=true; n=false; oper = oper.substr(1); }
        else if (!oper.empty() && oper[0] == '@') { n=true; i=false; oper = oper.substr(1); }
        else { n=true; i=true; }
        // index check
        string operNoIndex = oper;
        if (!operNoIndex.empty()) {
            size_t comma = operNoIndex.find(',');
            if (comma != string::npos) {
                string after = trim(operNoIndex.substr(comma+1));
                if (toUpper(after) == "X") { x = true; operNoIndex = trim(operNoIndex.substr(0, comma)); }
            }
        }
        // handle literals as operand (=...)
        bool operandIsLiteral = false;
        if (!operNoIndex.empty() && operNoIndex[0] == '=') operandIsLiteral = true;

        // handle immediate constant numeric #num
        bool immediateNumeric = false;
        uint32_t immediateValue = 0;
        if (i && !operNoIndex.empty() && (isdigit((unsigned char)operNoIndex[0]))) {
            try { immediateValue = (uint32_t)stoul(operNoIndex, nullptr, 0); immediateNumeric = true; } catch(...) { immediateNumeric=false; }
        }

        // compute location and addressing
        uint32_t instrAddrAbs = BLOCKTAB[r.block].startAddr + r.addr;
        int instrLen = isFormat4 ? 4 : 3;
        if (isFormat4) e = true; else e = false;

        if (immediateNumeric && !operandIsLiteral) {
            // immediate constant: put value into displacement field directly (no PC/Base-relative)
            if (!isFormat4 && immediateValue > 0xFFF) {
                // too big for format3 -> promote to format4
                e = true; isFormat4 = true; instrLen = 4;
            }
            uint32_t val = immediateValue;
            r.generatedObject = true;
            r.objectCode = buildFormat34(opcode, n, i, x, false, false, e, val);
            // If format4 used, add M record
            // We'll generate M records later when writing OBJFILE
            continue;
        }

        // operand could be symbol; compute its absolute address
        uint32_t targetAbs = 0; bool okTarget=false;
        if (operandIsLiteral) {
            // look up LITTAB
            auto it = LITTAB.find(operNoIndex);
            if (it != LITTAB.end() && it->second.hasAddr) {
                targetAbs = BLOCKTAB[it->second.block].startAddr + it->second.addr;
                okTarget = true;
            } else {
                logError(r.lineNo, "Literal not placed or unknown: " + operNoIndex);
                okTarget = false;
            }
        } else if (!operNoIndex.empty()) {
            // symbol -> SYMTAB
            auto it = SYMTAB.find(toUpper(operNoIndex));
            if (it != SYMTAB.end()) {
                targetAbs = BLOCKTAB[it->second.block].startAddr + it->second.addr;
                okTarget = true;
            } else {
                // maybe numeric (e.g., constant used without #)
                try { targetAbs = (uint32_t)stoul(operNoIndex, nullptr, 0); okTarget=true; } catch(...) { okTarget=false; }
            }
        } else {
            // no operand (e.g., RSUB)
            // RSUB is special: opcode with n=1,i=1 and target 0
            okTarget = false;
        }

        if (!okTarget && !immediateNumeric) {
            // For RSUB and similar, produce standard code
            if (opClean == "RSUB") {
                // RSUB: format3: opcode with n=1 i=1 and flags zero and disp 0
                r.generatedObject = true;
                r.objectCode = buildFormat34(opcode, true, true, false, false, false, false, 0);
                continue;
            } else {
                logError(r.lineNo, "Undefined operand or symbol: " + operNoIndex);
                continue;
            }
        }

        // if isFormat4 forced or if PC-relative out of range then use format4 (e==1)
        if (!isFormat4) {
            // try PC-relative: displacement = targetAbs - (instrAddrAbs + instrLen)
            int32_t disp = (int32_t)targetAbs - (int32_t)(instrAddrAbs + instrLen);
            if (disp >= -2048 && disp <= 2047) {
                p = true; b = false;
                uint32_t disp12 = (uint32_t)(disp & 0xFFF);
                r.generatedObject = true;
                r.objectCode = buildFormat34(opcode, n, i, x, b, p, false, disp12);
                continue;
            } else {
                // try base relative if baseOn
                if (baseOn) {
                    int32_t dispb = (int32_t)targetAbs - (int32_t)baseValue;
                    if (dispb >= 0 && dispb <= 4095) {
                        b = true; p = false;
                        uint32_t disp12 = (uint32_t)(dispb & 0xFFF);
                        r.generatedObject = true;
                        r.objectCode = buildFormat34(opcode, n, i, x, b, p, false, disp12);
                        continue;
                    } else {
                        // cannot fit in base or PC -> must use format4
                        isFormat4 = true; e = true; instrLen = 4;
                    }
                } else {
                    // no base, must use format4
                    isFormat4 = true; e = true; instrLen = 4;
                }
            }
        }
        if (isFormat4) {
            // format4: use direct 20-bit address (targetAbs), set M record
            r.generatedObject = true;
            r.objectCode = buildFormat34(opcode, n, i, x, false, false, true, targetAbs);
            continue;
        }
    } // end for INTLINES

    // Now we must also create object bytes for literals (LITTAB entries that were placed).
    // We'll create pseudo INTLINES entries for each literal in the order of their addresses to facilitate T record generation.
    struct LiteralPseudo { uint32_t absAddr; string block; string bytesHex; uint32_t length; string name; };
    vector<LiteralPseudo> litLines;
    for (auto &p : LITTAB) {
        if (p.second.hasAddr) {
            uint32_t abs = BLOCKTAB[p.second.block].startAddr + p.second.addr;
            stringstream ss;
            for (auto b : p.second.bytes) { ss << hex << uppercase << setw(2) << setfill('0') << (int)b; }
            litLines.push_back({abs, p.second.block, ss.str(), p.second.length, p.first});
        }
    }
    sort(litLines.begin(), litLines.end(), [](const LiteralPseudo &a, const LiteralPseudo &b){ return a.absAddr < b.absAddr; });

    // Prepare to produce OBJFILE: H, T, M, E records.
    // We'll gather object code fragments with absolute addresses then group into T records by block and contiguous ranges (and <=30 bytes).
    struct ObjFrag { uint32_t absAddr; string block; string bytes; };
    vector<ObjFrag> frags;

    // Gather fragments from INTLINES (generatedObject)
    for (auto &r : INTLINES) {
        if (r.generatedObject) {
            uint32_t abs = BLOCKTAB[r.block].startAddr + r.addr;
            frags.push_back({abs, r.block, r.objectCode});
        }
    }
    // add literals fragments
    for (auto &l : litLines) {
        frags.push_back({l.absAddr, l.block, l.bytesHex});
    }
    // sort fragments by absolute address
    sort(frags.begin(), frags.end(), [](const ObjFrag &a, const ObjFrag &b){
        if (a.block != b.block) return a.block < b.block;
        return a.absAddr < b.absAddr;
    });

    // For M records: collect addresses where format4 object codes exist
    vector<pair<uint32_t,int>> MRECS; // (absAddr+1, length in half-bytes) length 5 typical for format4
    for (auto &r : INTLINES) {
        if (r.generatedObject) {
            // detect if object is 8 hex digits => format4 -> M record at absAddr+1 len=5
            if (r.objectCode.size() == 8) {
                uint32_t abs = BLOCKTAB[r.block].startAddr + r.addr;
                MRECS.push_back({abs+1, 5}); // address where modification begins and length in half-bytes
            }
        }
    }

    // Now write OBJFILE
    ofstream objf("OBJFILE.obj");
    // Header: H programName startAddr programLength (each padded)
    string pname = programName;
    if (pname.size() > 6) pname = pname.substr(0,6);
    else pname = pname + string(6 - pname.size(), ' ');
    objf << "H" << pname << hexPad(programStart,6) << hexPad(programLength,6) << "\n";

    // Group fragments by block and contiguous addresses to form T records
    // We will iterate blocks in blockOrder and inside each block gather frags belonging to that block sorted by address
    for (auto &bn : blockOrder) {
        uint32_t blockStart = BLOCKTAB[bn].startAddr;
        // collect frags for this block
        vector<ObjFrag> bfrags;
        for (auto &f : frags) if (f.block == bn) bfrags.push_back(f);
        // if none, continue
        if (bfrags.empty()) continue;
        // sort by address
        sort(bfrags.begin(), bfrags.end(), [](const ObjFrag &a, const ObjFrag &b){ return a.absAddr < b.absAddr; });
        // iterate fragments to form T records: T startAddr len bytes
        size_t idx = 0;
        while (idx < bfrags.size()) {
            // start new T record at this fragment's address
            uint32_t tStart = bfrags[idx].absAddr;
            string tBytes = "";
            uint32_t tLenBytes = 0;
            // append fragments while contiguous and <=30 bytes and not crossing gaps (we allow small noncontig? must be contiguous)
            uint32_t expectedNext = tStart;
            while (idx < bfrags.size()) {
                auto &f = bfrags[idx];
                // convert hex string size into bytes count
                uint32_t fbyteLen = (uint32_t)(f.bytes.size() / 2);
                if (f.absAddr != expectedNext) {
                    // gap: cannot append; break to start new T record
                    break;
                }
                if (tLenBytes + fbyteLen > 30) break; // exceed T record limit
                // append
                tBytes += f.bytes;
                tLenBytes += fbyteLen;
                expectedNext += fbyteLen;
                ++idx;
            }
            if (tLenBytes == 0) {
                // can't pack any fragment (maybe fragment too large >30 bytes) -> emit single fragment as its own T record even if >30? but spec max 30.
                // We'll still emit it (rare). To avoid infinite loop, consume one.
                auto &f = bfrags[idx++];
                uint32_t fbyteLen = (uint32_t)(f.bytes.size()/2);
                objf << "T" << hexPad(f.absAddr,6) << hexPad(fbyteLen,2) << f.bytes << "\n";
            } else {
                objf << "T" << hexPad(tStart,6) << hexPad(tLenBytes,2) << tBytes << "\n";
            }
        }
    }

    // M records
    for (auto &m : MRECS) {
        objf << "M" << hexPad(m.first,6) << setw(2) << setfill('0') << uppercase << hex << m.second << "\n";
    }

    // End record: provide execution start (programStart)
    objf << "E" << hexPad(programStart,6) << "\n";
    objf.close();

    // Print summary and write list file (optional)
    cout << "=== PASS2 complete ===\n";
    cout << "Program length: " << hexPad(programLength,6) << "\n";
    cout << "OBJFILE.obj written. SYMTAB.txt and INTFILE.txt also available.\n";
    if (!ERRORS.empty()) {
        cout << "Errors/Warnings:\n";
        for (auto &e : ERRORS) cout << e << "\n";
    }
}

// --- MAIN ----------------------------------------------------------------
int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    cout << "SIC/XE 2-pass Assembler (C++)\n";
    string optabFile, srcFile;
    cout << "Enter optab filename (default: optab.txt): ";
    getline(cin, optabFile);
    if (trim(optabFile).empty()) optabFile = "optab.txt";
    cout << "Enter source filename to assemble (e.g., source.asm): ";
    getline(cin, srcFile);
    if (trim(srcFile).empty()) { cerr << "No source file specified. Exiting.\n"; return 1; }

    // Load OPTAB
    if (!loadOptab(optabFile)) { cerr << "Failed to load OPTAB from " << optabFile << "\n"; return 2; }
    cout << "OPTAB loaded: " << OPTAB.size() << " entries.\n";

    // Initialize default block
    BLOCKTAB.clear(); blockOrder.clear();
    BLOCKTAB["DEFAULT"] = Block{"DEFAULT",0,0,0,true};
    blockOrder.push_back("DEFAULT");

    // Pass1
    doPass1(srcFile);

    // Pass2
    doPass2(srcFile);

    cout << "Assembly finished.\n";
    return 0;
}
