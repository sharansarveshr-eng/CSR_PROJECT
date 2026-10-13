// Interactive CSR generator + SpMV reference model (C++17).
// Asks for rows, columns, the matrix and x; builds CSR; computes y = A*x;
// writes row_ptr/col_idx/val/x/y_golden .hex, spmv_params.vh and
// csr_and_result.txt to  ./out/  (separate from the Python outputs), and can
// deploy them to ../../hex/ for ModelSim / Quartus.
//
// Build:  g++ -std=c++17 -O2 -o spmv_interactive spmv_interactive.cpp
// Run  :  ./spmv_interactive      (run from the sw/cpp directory)

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static const fs::path OUT_DIR = "out";
static const fs::path HEX_DIR = "../../hex";
static const int64_t LO = -32768, HI = 32767;   // signed 16-bit data

struct CSR {
    std::vector<int> row_ptr, col_idx;
    std::vector<int> val;
};

// ---- input helpers -------------------------------------------------------
static bool read_line(std::string &s) {
    if (!std::getline(std::cin, s)) { std::cerr << "\nInput ended.\n"; std::exit(1); }
    return true;
}

static int ask_int(const std::string &prompt, int lo = 1, int hi = 65535) {
    for (;;) {
        std::cout << prompt << std::flush;
        std::string s; read_line(s);
        std::istringstream in(s);
        long long n; char extra;
        if ((in >> n) && !(in >> extra) && n >= lo && n <= hi) return (int)n;
        std::cout << "  Please enter an integer between " << lo << " and " << hi << ".\n";
    }
}

static std::vector<int> ask_list(const std::string &prompt, int count) {
    for (;;) {
        std::cout << prompt << std::flush;
        std::string s; read_line(s);
        for (char &c : s) if (c == ',') c = ' ';
        std::istringstream in(s);
        std::vector<int> v; std::string tok; bool ok = true;
        while (in >> tok) {
            try {
                size_t pos; long long n = std::stoll(tok, &pos, 0);
                if (pos != tok.size()) throw std::invalid_argument("x");
                if (n < LO || n > HI) {
                    std::cout << "  Values must be in " << LO << ".." << HI << ". Try again.\n";
                    ok = false; break;
                }
                v.push_back((int)n);
            } catch (...) {
                std::cout << "  Only integers are allowed (decimal or 0x hex). Try again.\n";
                ok = false; break;
            }
        }
        if (!ok) continue;
        if ((int)v.size() != count) {
            std::cout << "  Expected " << count << " numbers, got " << v.size() << ". Try again.\n";
            continue;
        }
        return v;
    }
}

// ---- core ----------------------------------------------------------------
static CSR dense_to_csr(const std::vector<std::vector<int>> &A) {
    CSR c; c.row_ptr.push_back(0);
    for (const auto &row : A) {
        for (size_t j = 0; j < row.size(); ++j)
            if (row[j] != 0) { c.col_idx.push_back((int)j); c.val.push_back(row[j]); }
        c.row_ptr.push_back((int)c.val.size());
    }
    return c;
}

// Same loop structure as rtl/spmv.v
static std::vector<int64_t> spmv(const CSR &c, const std::vector<int> &x) {
    size_t n_rows = c.row_ptr.size() - 1;
    std::vector<int64_t> y(n_rows, 0);
    for (size_t i = 0; i < n_rows; ++i)
        for (int k = c.row_ptr[i]; k < c.row_ptr[i + 1]; ++k)
            y[i] += (int64_t)c.val[k] * x[c.col_idx[k]];
    return y;
}

static std::string hexw(int64_t v, int bits) {
    uint64_t mask = (bits == 64) ? ~0ULL : ((1ULL << bits) - 1);
    std::ostringstream o;
    o << std::hex << std::setfill('0') << std::setw(bits / 4) << ((uint64_t)v & mask);
    return o.str();
}

template <typename T>
static void write_hex(const fs::path &p, const std::vector<T> &w, int bits) {
    std::ofstream f(p);
    for (T v : w) f << hexw((int64_t)v, bits) << "\n";
}

template <typename T>
static std::string vec_str(const std::vector<T> &v) {
    std::ostringstream o; o << "[";
    for (size_t i = 0; i < v.size(); ++i) o << (i ? ", " : "") << v[i];
    o << "]"; return o.str();
}

int main() {
    std::cout << "=== SpMV interactive matrix entry (C++) ===\n";
    int rows = ask_int("Number of rows    : ");
    int cols = ask_int("Number of columns : ");

    std::cout << "\nEnter the matrix, one row per line (" << cols
              << " numbers per row, 0 for empty entries):\n";
    std::vector<std::vector<int>> A;
    for (int i = 0; i < rows; ++i) {
        std::ostringstream p; p << "  row " << std::setw(2) << i << ": ";
        A.push_back(ask_list(p.str(), cols));
    }
    std::cout << "\nEnter the input vector x (" << cols << " numbers):\n";
    std::vector<int> x = ask_list("  x     : ", cols);

    CSR c = dense_to_csr(A);
    if (c.val.empty()) { std::cerr << "\nThe matrix has no non-zero entries.\n"; return 1; }
    std::vector<int64_t> y = spmv(c, x);
    for (int64_t v : y)
        if (v < INT32_MIN || v > INT32_MAX) {
            std::cerr << "\nResult does not fit in the 32-bit accumulator.\n"; return 1;
        }

    fs::create_directories(OUT_DIR);
    write_hex(OUT_DIR / "row_ptr.hex", c.row_ptr, 16);
    write_hex(OUT_DIR / "col_idx.hex", c.col_idx, 16);
    write_hex(OUT_DIR / "val.hex", c.val, 16);
    write_hex(OUT_DIR / "x.hex", x, 16);
    write_hex(OUT_DIR / "y_golden.hex", y, 32);
    {
        std::ofstream f(OUT_DIR / "spmv_params.vh");
        f << "`ifndef SPMV_PARAMS_VH\n`define SPMV_PARAMS_VH\n"
          << "`define N_ROWS " << rows << "\n`define N_COLS " << cols
          << "\n`define NNZ " << c.val.size() << "\n`endif\n";
    }

    std::ostringstream r;
    r << "rows = " << rows << "\ncols = " << cols << "\nnnz  = " << c.val.size()
      << "\n\nMatrix A:\n";
    for (const auto &row : A) {
        r << " ";
        for (int v : row) r << " " << std::setw(6) << v;
        r << "\n";
    }
    r << "\nx        = " << vec_str(x) << "\n\nCSR:\nrow_ptr = " << vec_str(c.row_ptr)
      << "\ncol_idx = " << vec_str(c.col_idx) << "\nval     = " << vec_str(c.val)
      << "\n\nResult y = A*x:\n";
    for (size_t i = 0; i < y.size(); ++i) r << "  y[" << i << "] = " << y[i] << "\n";
    std::ofstream(OUT_DIR / "csr_and_result.txt") << r.str();

    std::cout << "\n" << r.str() << "\nFiles written to: " << fs::absolute(OUT_DIR) << "\n";

    std::cout << "\nCopy these files to " << HEX_DIR.string()
              << " for simulation/Quartus? [y/N]: " << std::flush;
    std::string ans; std::getline(std::cin, ans);
    if (!ans.empty() && (ans[0] == 'y' || ans[0] == 'Y')) {
        fs::create_directories(HEX_DIR);
        for (const char *n : {"row_ptr.hex", "col_idx.hex", "val.hex", "x.hex",
                              "y_golden.hex", "spmv_params.vh"})
            fs::copy_file(OUT_DIR / n, HEX_DIR / n, fs::copy_options::overwrite_existing);
        std::cout << "Deployed to " << fs::absolute(HEX_DIR) << "\n"
                  << "Now run ModelSim (sim/run_sim.do) or recompile in Quartus.\n";
    }
    return 0;
}