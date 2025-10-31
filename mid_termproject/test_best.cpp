#include <bits/stdc++.h>
using namespace std;

// ----------------------------- Utility helpers -----------------------------
static string trim(const string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a==string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b-a+1);
}

static vector<string> split_whitespace(const string &s) {
    vector<string> out;
    istringstream iss(s);
    string w;
    while (iss >> w) out.push_back(w);
    return out;
}

static bool is_number(const string &s) {
    if (s.empty()) return false;
    for (char c: s) if (!isdigit((unsigned char)c)) return false;
    return true;
}

static int hex_to_int(const string &h) {
    return (int)strtol(h.c_str(), nullptr, 16);
}

static string int_to_hex(int v, int width=0) {
    std::ostringstream oss;
    oss << std::uppercase << std::hex << v;
    string s = oss.str();
    if ((int)s.size() < width) s = string(width - s.size(), '0') + s;
    return s;
}

// ----------------------------- Data Structures -----------------------------

struct OptabEntry { string mnemonic; string opcode; int format; /*format guess*/ };

struct SymtabEntry {
    string name;
    int addr;      // relative address inside block
    string block;  // block name
    bool defined;
    bool is_absolute;
    string comment;
};

struct LittabEntry {
    string literal; // e.g. =C'EOF'
    string value;   // evaluated bytes as hex string
    int length;     // byte length
    int addr;       // assigned during LTORG/END
    bool addr_assigned;
};

struct BlockEntry {
    string name;
    int start_addr; // will be assigned at end of pass1 (cumulative)
    int length;     // final LOCCTR for block
    int locctr;     // working LOCCTR during pass1 for this block
};

struct SourceLine {
    int lineno;
    string label;
    string op;
    string operand;
    string comment;
    int addr;      // assigned during pass1 (relative to block)
    string block;
    string raw;
};

// ----------------------------- Assembler class -----------------------------

class Assembler {
public:
    Assembler(const string &src) : sourceFile(src) {
        // initialize default block
        blocks["DEFAULT"] = {"DEFAULT", 0, 0, 0};
        currentBlock = "DEFAULT";
        programStart = 0; programName = "";
    }

    bool loadOptab(const string &optabFile="optab.txt") {
        ifstream inf(optabFile);
        if (!inf) {
            cerr << "ERROR: cannot open optab.txt\n";
            return false;
        }
        string line;
        while (getline(inf,line)) {
            line = trim(line);
            if (line.empty()) continue;
            auto parts = split_whitespace(line);
            if (parts.size() < 2) continue;
            string mnem = parts[0];
            string opc = parts[1];
            optab[mnem] = {mnem, opc, 3}; // default format guess=3; '+' prefix indicates format4 at use time
        }
        cout << "Loaded OPTAB entries: " << optab.size() << "\n";
        return true;
    }

    bool runPass1() {
        ifstream inf(sourceFile);
        if (!inf) { cerr << "ERROR: cannot open source file: "<<sourceFile<<"\n"; return false; }

        string line;
        int lineno=0;
        int locctr = 0;
        bool started=false;

        while (getline(inf, line)) {
            lineno++;
            string raw = line;
            // remove comment starting with '.' (SIC style)
            string working = raw;
            string comment = "";
            size_t pos = working.find('.');
            if (pos!=string::npos) {
                comment = working.substr(pos);
                working = working.substr(0,pos);
            }
            // parse columns: label (col1), opcode (col2), operand (rest)
            // allow flexible whitespace-separated parsing
            // ------------------- Source line parsing (Pass1) -------------------
            auto tokens = split_whitespace(working);
            string label = "", op = "", operand = "";

            // 빈 줄 또는 주석만 있는 경우
            if (tokens.size() == 0) {
                SourceLine sl{lineno, label, op, operand, comment, -1, currentBlock, raw};
                sourceLines.push_back(sl);
                continue;
            }

            // START 라인 처리
            if (tokens[0] == "START") {
                op = "START";
                size_t op_pos = working.find(op);
                operand = trim(working.substr(op_pos + op.size()));
                label = tokens.size() > 1 ? tokens[0] : "";
                programStart = operand.empty() ? 0 : stoi(operand);
                locctr = programStart;
                programName = label.empty() ? "" : label;
                SourceLine sl{lineno, label, op, operand, comment, locctr, currentBlock, raw};
                sourceLines.push_back(sl);
                continue;
            }

            // label 판단: 첫 번째 token이 opcode 또는 directive가 아니면 label
            int idx = 0;
            if (!optab.count(tokens[0]) && !isDirective(tokens[0]) && tokens[0][0] != '+') {
                label = tokens[0];
                idx = 1;
            }

            // opcode
            if (idx < tokens.size()) {
                op = tokens[idx++];
            }

            // operand: opcode 위치 이후 전체 문자열
            size_t op_pos = working.find(op);
            if (op_pos != string::npos) {
                operand = trim(working.substr(op_pos + op.size()));
            }

            // 결과 저장
            SourceLine sl{lineno, label, op, operand, comment, -1, currentBlock, raw};

            // PROCESS line for pass1
            if (!started) {
                if (op=="START") {
                    started = true;
                    programStart = operand.empty() ? 0 : stoi(operand);
                    locctr = programStart;
                    sl.addr = locctr;
                    programName = label.empty() ? "" : label;
                    cout << "Program START at "<<programStart<<"\n";
                    sourceLines.push_back(sl);
                    continue;
                } else {
                    // no START: assume start at 0
                    started = true;
                    programStart = 0;
                    locctr = 0;
                }
            }

            // assign address to this source line (relative to block)
            sl.addr = blocks[currentBlock].locctr;
            sl.block = currentBlock;

            // If there is a label, insert into SYMTAB
            if (!label.empty()) {
                if (symtab.count(label)) {
                    // duplicate label -> mark error
                    symtab[label].comment += " DUPLICATE";
                    cout << "Warning: duplicate label '"<<label<<"' at line "<<lineno<<"\n";
                } else {
                    SymtabEntry e{label, blocks[currentBlock].locctr, currentBlock, true, false, ""};
                    symtab[label] = e;
                }
            }

            // handle directives and instructions for LOCCTR increment
            if (op=="END") {
                // process literals at END
                processLiteralsAt(sl.addr);
                sourceLines.push_back(sl);
                break; // stop reading further lines for now
            } else if (op=="RESW") {
                int n = is_number(operand) ? stoi(operand) : 0;
                blocks[currentBlock].locctr += 3*n;
            } else if (op=="RESB") {
                int n = is_number(operand) ? stoi(operand) : 0;
                blocks[currentBlock].locctr += n;
            } else if (op=="WORD") {
                blocks[currentBlock].locctr += 3;
            } else if (op=="BYTE") {
                // operand like C'EOF' or X'F1'
                int inc = evaluateByteLength(operand);
                blocks[currentBlock].locctr += inc;
            } else if (op=="LTORG") {
                // assign addresses to pending literals
                processLiteralsAt(blocks[currentBlock].locctr);
            } else if (op=="EQU") {
                // naive: only support simple numeric constants or '*'
                if (operand=="*") {
                    if (label.empty()) {
                        cerr<<"EQU * without label at line "<<lineno<<"\n";
                    } else {
                        symtab[label] = {label, blocks[currentBlock].locctr, currentBlock, true, true, "EQU *"};
                    }
                } else if (is_number(operand)) {
                    if (label.empty()) cerr<<"EQU without label at line"<<lineno<<"\n";
                    else symtab[label] = {label, stoi(operand), currentBlock, true, true, "EQU const"};
                } else {
                    // expression support TODO
                    cout<<"Note: complex EQU expression not implemented at line "<<lineno<<"\n";
                }
            } else if (op=="ORG") {
                // set LOCCTR to operand value (naive: decimal or symbol)
                if (is_number(operand)) {
                    blocks[currentBlock].locctr = stoi(operand);
                } else if (symtab.count(operand)) {
                    blocks[currentBlock].locctr = symtab[operand].addr;
                } else {
                    cout<<"Warning: ORG operand unknown at line "<<lineno<<"\n";
                }
            } else if (op=="BASE") {
                // record base usage for pass2
                baseRegisterSymbol = operand;
            } else if (op=="USE") {
                // switch block
                string name = operand.empty() ? "DEFAULT" : operand;
                if (!blocks.count(name)) {
                    // create new block starting LOCCTR=0
                    blocks[name] = {name,0,0,0};
                }
                // save current locctr already stored in blocks map
                currentBlock = name;
            } else if (op.size()>0 && op[0]=='+') {
                // format 4 instruction
                string m = op.substr(1);
                // add literal/instruction length 4
                blocks[currentBlock].locctr += 4;
            } else if (optab.count(op)) {
                // normal opcode - assume format 3 (length 3)
                blocks[currentBlock].locctr += 3;
            } else {
                // possible directive or unknown opcode - handle as error or ignore
                // detect literals in operand
                if (!operand.empty() && operand[0]=='=') {
                    if (!littab.count(operand)) {
                        LittabEntry le{operand, "", 0, 0, false};
                        // evaluate literal value and length now for BYTE/X/C forms
                        auto pv = evaluateLiteral(operand);
                        le.value = pv.first;
                        le.length = pv.second;
                        littab[operand] = le;
                    }
                }
                // else unknown opcode
                if (!isDirective(op)) {
                    cout<<"Warning: unknown opcode/directive '"<<op<<"' at line "<<lineno<<"\n";
                    // assume 3 bytes to avoid getting stuck
                    blocks[currentBlock].locctr += 3;
                }
            }

            sourceLines.push_back(sl);
        }

        // finalize block lengths (each block's locctr is its length)
        for (auto &p: blocks) {
            p.second.length = p.second.locctr;
        }

        // compute start addresses for blocks: assign increasing addresses starting at programStart
        int cumulative = programStart;
        for (auto &p: blocks) {
            // preserve iteration order? unordered_map - to ensure stable order, sort keys
        }
        // produce ordered block list by insertion order: build from map keys
        vector<string> blockNames;
        for (auto &kv : blocks) blockNames.push_back(kv.first);
        // ensure DEFAULT is first
        sort(blockNames.begin(), blockNames.end());
        // assign starts
        cumulative = programStart;
        for (string &bn : blockNames) {
            blocks[bn].start_addr = cumulative;
            cumulative += blocks[bn].length;
        }

        // adjust symbol addresses to absolute addresses (start_addr + relative addr)
        for (auto &kv: symtab) {
            kv.second.addr = blocks[kv.second.block].start_addr + kv.second.addr;
        }

        // assign littab addresses that weren't assigned inside LTORG handling
        int lastLoc = 0; // find block DEFAULT's loc
        for (auto &p: blocks) if (p.first=="DEFAULT") lastLoc = p.second.start_addr + p.second.length;
        // naive: assign literals after program end in DEFAULT block
        int litAddr = lastLoc;
        for (auto &kv: littab) {
            if (!kv.second.addr_assigned) {
                kv.second.addr = litAddr;
                kv.second.addr_assigned = true;
                litAddr += kv.second.length;
            }
        }

        // write SYMTAB.txt and INTFILE
        writeSYMTAB();
        writeINTFILE();

        cout<<"PASS1 complete. SYMTAB.txt and INTFILE generated.\n";
        return true;
    }
    
    bool runPass2() {
        ofstream objf("OBJFILE");
        if (!objf) { cerr << "Cannot open OBJFILE\n"; return false; }

        // 헤더 레코드
        string progNameFixed = programName;
        if (progNameFixed.size() > 6) progNameFixed = progNameFixed.substr(0,6);
        while (progNameFixed.size() < 6) progNameFixed += ' ';
        objf << "H" << progNameFixed << int_to_hex(programStart,6);

        int progLength = 0;
        for (auto &p: blocks) progLength += p.second.length;
        objf << int_to_hex(progLength,6) << "\n";

        int t_start = -1;
        string t_record = "";

        auto flushTRecord = [&](int addr) {
            if (!t_record.empty()) {
                int len = t_record.size()/2;
                objf << "T" << int_to_hex(t_start,6) << int_to_hex(len,2) << t_record << "\n";
                t_record.clear();
            }
            t_start = addr;
        };

        // 모든 source line 처리
        for (auto &sl: sourceLines) {
            string obj = "";

            // BYTE
            if (sl.op=="BYTE") {
                string opd = trim(sl.operand);
                if (opd.size() > 2 && opd[0]=='C' && opd[1]=='\'') {
                    size_t p2 = opd.find('\'',2);
                    string content = (p2==string::npos) ? opd.substr(2) : opd.substr(2,p2-2);
                    for (char c: content) obj += int_to_hex((unsigned char)c,2);
                } else if (opd.size() > 2 && opd[0]=='X' && opd[1]=='\'') {
                    size_t p2 = opd.find('\'',2);
                    obj = (p2==string::npos) ? opd.substr(2) : opd.substr(2,p2-2);
                }
            } 
            // WORD
            else if (sl.op=="WORD") {
                int val = is_number(sl.operand) ? stoi(sl.operand) : 0;
                obj = int_to_hex(val,6);
            }
            // Instruction
            else if (!sl.op.empty()) {
                string op = sl.op;
                bool format4 = false;
                if (op[0]=='+') { format4=true; op = op.substr(1); }

                if (optab.count(op)) {
                    string opc = optab[op].opcode;
                    obj = opc;

                    int addr = 0;
                    bool indexed = false;
                    if (!sl.operand.empty()) {
                        string operand = sl.operand;
                        size_t comma = operand.find(',');
                        if (comma != string::npos) {
                            string tail = operand.substr(comma+1);
                            if (tail=="X") indexed = true;
                            operand = operand.substr(0,comma);
                        }

                        if (is_number(operand)) addr = stoi(operand);
                        else if (symtab.count(operand)) addr = symtab[operand].addr;
                        else if (littab.count(operand)) addr = littab[operand].addr;
                    }

                    if (format4) {
                        // format4: 4 bytes, xbit 반영
                        if (indexed) addr |= 0x80000; // x bit in format4 (bit20)
                        obj += int_to_hex(addr,5);
                    } else {
                        // format3: 3 bytes, xbit 반영
                        if (indexed) addr |= 0x8000; // x bit in format3 (bit15)
                        obj += int_to_hex(addr,4);
                    }
                }
                // literal
                else if (!sl.operand.empty() && sl.operand[0]=='=' && littab.count(sl.operand)) {
                    obj = littab[sl.operand].value;
                }
            }

            if (!obj.empty()) {
                if (t_record.empty()) t_start = sl.addr;
                if ((t_record.size()+obj.size())/2 > 30) flushTRecord(sl.addr);
                t_record += obj;
            }
        }

        flushTRecord(t_start);

        // M 레코드 처리 format4
        for (auto &sl: sourceLines) {
            if (!sl.op.empty() && sl.op[0]=='+') {
                int addr = sl.addr;
                objf << "M" << int_to_hex(addr,6) << "05\n";
            }
        }

        // E 레코드
        objf << "E" << int_to_hex(programStart,6) << "\n";

        objf.close();
        return true;
    }

private:
    string sourceFile;
    unordered_map<string, OptabEntry> optab;
    map<string, SymtabEntry> symtab; // ordered for output
    map<string, LittabEntry> littab;
    map<string, BlockEntry> blocks; // blockname->entry
    vector<SourceLine> sourceLines;
    string currentBlock;
    string programName;
    int programStart;
    string baseRegisterSymbol;

    bool isDirective(const string &s) {
        static unordered_set<string> directives = {"START","END","RESW","RESB","WORD","BYTE","BASE","LTORG","EQU","ORG","USE"};
        return directives.count(s);
    }

    pair<string,int> evaluateLiteral(const string &lit) {
        // lit like =C'EOF' or =X'F1' or =W'05' (simple)
        if (lit.size()>2 && lit[1]=='C' && lit[2]=='\'') {
            // =C'...'
            size_t p2 = lit.find('\'',3);
            string content = (p2==string::npos) ? string() : lit.substr(3,p2-3);
            string hex="";
            for (char c: content) {
                unsigned char uc = (unsigned char)c;
                hex += int_to_hex(uc,2);
            }
            return {hex, (int)content.size()};
        } else if (lit.size()>2 && lit[1]=='X' && lit[2]=='\'') {
            size_t p2 = lit.find('\'',3);
            string content = (p2==string::npos) ? string() : lit.substr(3,p2-3);
            return {content, (int)(content.size()/2)}; // content should be hex digits
        } else {
            // numeric literal or unsupported: treat as integer/WORD
            if (lit.size()>1 && lit[0]=='=' && is_number(lit.substr(1))) {
                int v = stoi(lit.substr(1));
                string h = int_to_hex(v,6);
                return {h,3};
            }
        }
        return {"",0};
    }

    int evaluateByteLength(const string &operand) {
        string op = trim(operand);
        if (op.size() > 2 && op[0]=='C' && op[1]=='\'') {
            size_t p2 = op.rfind('\''); // 마지막 따옴표 찾기
            if (p2 == string::npos) return static_cast<int>(op.size()-2);
            return static_cast<int>(p2 - 2); // 실제 문자 개수
        } 
        else if (op.size() > 2 && op[0]=='X' && op[1]=='\'') {
            size_t p2 = op.rfind('\'');
            if (p2 == string::npos) return 1;
            int len = static_cast<int>(p2 - 2);
            return (len + 1)/2;
        }
        return 1;
    }

    void processLiteralsAt(int loc) {
        // assign addresses to littab entries that have not been assigned, sequentially at loc
        for (auto &kv: littab) {
            if (!kv.second.addr_assigned) {
                kv.second.addr = loc;
                kv.second.addr_assigned = true;
                loc += kv.second.length;
            }
        }
        // update current block's locctr accordingly (naive: apply to current block)
        blocks[currentBlock].locctr = loc;
    }

    void writeSYMTAB() {
        ofstream outf("SYMTAB.txt");
        cout<<"SYMTAB:\n";
        for (auto &kv: symtab) {
            outf << kv.first << " " << int_to_hex(kv.second.addr,4) << " " << kv.second.block << " " << kv.second.comment << "\n";
            cout << kv.first << " -> "<< int_to_hex(kv.second.addr,4) << " (block="<<kv.second.block<<")\n";
        }
        outf.close();
    }

    void writeINTFILE() {
        ofstream outf("INTFILE");
        cout<<"Intermediate File:\n";
        for (auto &sl: sourceLines) {
            outf << setw(4) << sl.lineno << " " << (sl.addr>=0?int_to_hex(sl.addr,4):"----") << " " << sl.block << " " << sl.raw << "\n";
            cout << setw(4) << sl.lineno << " " << (sl.addr>=0?int_to_hex(sl.addr,4):"----") << " " << sl.block << " " << sl.raw << "\n";
        }
        // write literal table
        outf << "\nLITTAB:\n";
        cout<<"LITTAB:\n";
        for (auto &kv: littab) {
            outf << kv.first << " addr=" << (kv.second.addr_assigned?int_to_hex(kv.second.addr,4):"----") << " val="<<kv.second.value<<" len="<<kv.second.length<<"\n";
            cout << kv.first << " -> addr="<<(kv.second.addr_assigned?int_to_hex(kv.second.addr,4):"----")<<" val="<<kv.second.value<<" len="<<kv.second.length<<"\n";
        }
        outf.close();
    }

    string assembleLineToObject(const SourceLine &sl) {
        string obj = "";

        // BYTE
        if (sl.op == "BYTE") {
            string opd = trim(sl.operand);
            if (opd.size() > 2 && opd[0] == 'C' && opd[1] == '\'') {
                size_t p2 = opd.find('\'', 2);
                string content = (p2 == string::npos) ? opd.substr(2) : opd.substr(2, p2 - 2);
                for (char c : content) obj += int_to_hex((unsigned char)c, 2);
            } else if (opd.size() > 2 && opd[0] == 'X' && opd[1] == '\'') {
                size_t p2 = opd.find('\'', 2);
                obj = (p2 == string::npos) ? opd.substr(2) : opd.substr(2, p2 - 2);
            }
            return obj;
        }

        // WORD
        if (sl.op == "WORD") {
            int val = is_number(sl.operand) ? stoi(sl.operand) : 0;
            obj = int_to_hex(val, 6);
            return obj;
        }

        // Instruction
        string op = sl.op;
        bool format4 = false;
        if (!op.empty() && op[0] == '+') { format4 = true; op = op.substr(1); }

        if (optab.count(op)) {
            string opc = optab[op].opcode;
            obj = opc;

            if (!sl.operand.empty()) {
                string operand = sl.operand;
                bool indexed = false;
                size_t comma = operand.find(',');
                if (comma != string::npos) {
                    string tail = operand.substr(comma + 1);
                    if (tail == "X") indexed = true;
                    operand = operand.substr(0, comma);
                }

                int addr = 0;
                if (is_number(operand)) addr = stoi(operand);
                else if (symtab.count(operand)) addr = symtab[operand].addr;
                else if (littab.count(operand)) addr = littab[operand].addr;

                if (format4) obj += int_to_hex(addr, 5); // format4=4bytes, opcode 1byte+addr 4bytes
                else obj += int_to_hex(addr, 4);        // format3=3bytes, opcode 1byte+addr 2bytes

                if (indexed) obj += "(X)";
            }
            return obj;
        }

        // literal
        if (!sl.operand.empty() && sl.operand[0] == '=' && littab.count(sl.operand)) {
            obj = littab[sl.operand].value;
            return obj;
        }

        // directive or unknown opcode -> no object code
        return "";
    }
   
};

// ----------------------------- main -----------------------------
int main(int argc, char** argv) {
    cout<<"SIC/XE 2-pass assembler - baseline implementation";
    string src;
    if (argc >= 2) {
        // use command-line argument if provided
        src = argv[1];
    } else {
        // prompt the user for the source filename interactively
        cout << "Enter source filename to assemble: ";
        if (!std::getline(cin, src)) {
            cerr << "No input received. Exiting.";
            return 1;
        }
        // trim whitespace
        src = trim(src);
        if (src.empty()) {
            cerr << "Empty filename provided. Exiting.";
            return 1;
        }
    }

    Assembler asmblr(src);
    if (!asmblr.loadOptab("optab.txt")) return 2;
    if (!asmblr.runPass1()) return 3;
    if (!asmblr.runPass2()) return 4;
    cout<<"All done. Check SYMTAB.txt, INTFILE, OBJFILE.";
    return 0;
}
