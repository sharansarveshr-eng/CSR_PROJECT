// create_csr_interactive.cpp -- automatically generates a sparse matrix A
// (from your chosen size) and a matching random H, converts A into CSR
// format, and writes out row_ptr.txt, col_idx.txt, val.txt, H.txt, and
// expected_out.txt (the result of A @ H) into an output folder.
//
// Rules for the auto-generated matrix A:
//   * zeros are always the MAJORITY of the matrix
//   * 1-digit non-zero values (1-9) are always present, at least 2 of them
//   * you choose 2-digit (10-99) or 3-digit (100-999) non-zero values, and
//     at least 2 of those appear too
//   * optionally BOTH 2-digit and 3-digit values are included, at least 2 of each
//   * at most 3 non-zeros per row (keeps results inside the Q16.16 range)
//
// Run it with no arguments:
//     ./create_csr_interactive

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <random>
#include <algorithm>
#include <numeric>

#ifdef _WIN32
    #include <direct.h>
#else
    #include <sys/stat.h>
#endif

using Matrix = std::vector<std::vector<double>>;

static const int MIN_PER_CATEGORY = 2;  // each included digit-length appears at least this many times
static const int MAX_NNZ_PER_ROW  = 3;  // cap per row

static void make_dir(const std::string& path) {
#ifdef _WIN32
    _mkdir(path.c_str());
#else
    mkdir(path.c_str(), 0755);
#endif
}

// Category 0 = 1-digit (1-9), 1 = 2-digit (10-99), 2 = 3-digit (100-999)
static int value_for_category(int cat, std::mt19937& rng) {
    static const int lo[3] = {1, 10, 100};
    static const int hi[3] = {9, 99, 999};
    std::uniform_int_distribution<int> d(lo[cat], hi[cat]);
    return d(rng);
}

// Generates a SPARSE rows x cols matrix with exactly nnz_total non-zeros.
// 1-digit values are always used; 2-digit / 3-digit values are used if
// use2 / use3 are set. Every included category appears at least
// MIN_PER_CATEGORY times, and no row holds more than MAX_NNZ_PER_ROW.
static Matrix generate_sparse_matrix(int rows, int cols, bool use2, bool use3, int nnz_total) {
    std::random_device rd;
    std::mt19937 rng(rd());

    std::vector<int> allowed = {0};
    if (use2) allowed.push_back(1);
    if (use3) allowed.push_back(2);

    // Guarantee the minimum count of every included category first,
    // then fill the rest with a random mix of the included categories.
    std::vector<int> cats;
    for (int c : allowed)
        for (int k = 0; k < MIN_PER_CATEGORY; k++) cats.push_back(c);
    std::uniform_int_distribution<int> pick(0, (int)allowed.size() - 1);
    while ((int)cats.size() < nnz_total) cats.push_back(allowed[pick(rng)]);
    std::shuffle(cats.begin(), cats.end(), rng);

    // Place them in distinct random cells, at most MAX_NNZ_PER_ROW per row.
    int per_row_cap = std::min(MAX_NNZ_PER_ROW, cols);
    std::vector<int> cells(rows * cols);
    std::iota(cells.begin(), cells.end(), 0);
    std::shuffle(cells.begin(), cells.end(), rng);

    std::vector<int> row_count(rows, 0);
    Matrix M(rows, std::vector<double>(cols, 0.0));
    size_t placed = 0;
    for (int idx : cells) {
        if (placed == cats.size()) break;
        int i = idx / cols, j = idx % cols;
        if (row_count[i] >= per_row_cap) continue;
        M[i][j] = value_for_category(cats[placed], rng);
        row_count[i]++;
        placed++;
    }
    return M;
}

// Automatically generates a DENSE r x c matrix with random values 0-9.
static Matrix generate_dense_matrix(int rows, int cols) {
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<int> val_pick(0, 9);

    Matrix M(rows, std::vector<double>(cols));
    for (int i = 0; i < rows; i++)
        for (int j = 0; j < cols; j++)
            M[i][j] = val_pick(rng);
    return M;
}

int main() {
    int a_rows, a_cols;
    std::cout << "Enter number of rows (m) for matrix A: ";
    std::cin >> a_rows;
    std::cout << "Enter number of columns (n) for matrix A: ";
    std::cin >> a_cols;

    if (a_rows <= 0 || a_cols <= 0) {
        std::cerr << "Error: matrix dimensions must be positive.\n";
        return 1;
    }

    // ---- Option 1: 2-digit or 3-digit non-zero values ----
    int digits;
    std::cout << "Enter digit length for non-zero values in A (2 or 3): ";
    std::cin >> digits;
    if (digits != 2 && digits != 3) {
        std::cerr << "Error: please enter 2 or 3.\n";
        return 1;
    }

    // ---- Option 2: include BOTH 2-digit and 3-digit values? ----
    char both_ans;
    std::cout << "Also include " << (digits == 2 ? "3-digit" : "2-digit")
              << " values, so both 2-digit and 3-digit numbers appear? (y/n): ";
    std::cin >> both_ans;
    bool both;
    if (both_ans == 'y' || both_ans == 'Y') both = true;
    else if (both_ans == 'n' || both_ans == 'N') both = false;
    else {
        std::cerr << "Error: please enter y or n.\n";
        return 1;
    }
    bool use2 = (digits == 2) || both;
    bool use3 = (digits == 3) || both;

    // ---- Work out how many non-zeros this matrix can hold ----
    // required: at least MIN_PER_CATEGORY of the 1-digit values, plus of each
    // chosen multi-digit length. hard_cap: zeros must strictly outnumber
    // non-zeros, and no row may exceed MAX_NNZ_PER_ROW.
    int required = MIN_PER_CATEGORY * (1 + (use2 ? 1 : 0) + (use3 ? 1 : 0));
    int cells = a_rows * a_cols;
    int hard_cap = std::min((cells - 1) / 2, a_rows * std::min(MAX_NNZ_PER_ROW, a_cols));
    if (required > hard_cap) {
        std::cerr << "\nError: a " << a_rows << "x" << a_cols << " matrix is too small.\n"
                  << "Your choices need at least " << required << " non-zero values, but this size can hold at most "
                  << std::max(hard_cap, 0) << " while keeping zeros in the majority (max "
                  << MAX_NNZ_PER_ROW << " per row).\n"
                  << "Please rerun with a larger matrix.\n";
        return 1;
    }
    // Keep it clearly sparse: aim for about 25% non-zeros at most.
    int soft_cap = std::min(hard_cap, std::max(required, cells / 4));
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<int> nnz_pick(required, soft_cap);
    int nnz_total = nnz_pick(rng);

    Matrix A = generate_sparse_matrix(a_rows, a_cols, use2, use3, nnz_total);
    std::cout << "\nAuto-generated sparse matrix A (" << a_rows << "x" << a_cols << "):\n";
    for (auto& row : A) {
        for (size_t j = 0; j < row.size(); j++) {
            std::cout << row[j];
            if (j + 1 < row.size()) std::cout << " ";
        }
        std::cout << "\n";
    }

    // Summary of what was generated
    int c1 = 0, c2 = 0, c3 = 0;
    for (auto& row : A)
        for (double v : row) {
            if (v == 0.0) continue;
            if (v < 10) c1++; else if (v < 100) c2++; else c3++;
        }
    int nz = c1 + c2 + c3;
    std::cout << "\nNon-zeros: " << nz << " (1-digit: " << c1 << ", 2-digit: " << c2
              << ", 3-digit: " << c3 << "), zeros: " << (cells - nz) << "\n";

    int h_rows = a_cols;
    int h_cols = 1;

    Matrix H = generate_dense_matrix(h_rows, h_cols);
    std::cout << "\nAuto-generated matrix H (" << h_rows << "x" << h_cols << "):\n";
    for (auto& row : H) {
        for (size_t j = 0; j < row.size(); j++) {
            std::cout << row[j];
            if (j + 1 < row.size()) std::cout << " ";
        }
        std::cout << "\n";
    }

    std::vector<int> row_ptr(a_rows + 1, 0);
    std::vector<int> col_idx;
    std::vector<double> val;

    for (int i = 0; i < a_rows; i++) {
        for (int j = 0; j < a_cols; j++) {
            if (A[i][j] != 0.0) {
                col_idx.push_back(j);
                val.push_back(A[i][j]);
            }
        }
        row_ptr[i + 1] = (int)col_idx.size();
    }

    Matrix Z(a_rows, std::vector<double>(h_cols, 0.0));
    for (int i = 0; i < a_rows; i++) {
        for (int k = row_ptr[i]; k < row_ptr[i + 1]; k++) {
            int j = col_idx[k];
            double v = val[k];
            for (int f = 0; f < h_cols; f++) {
                Z[i][f] += v * H[j][f];
            }
        }
    }

    std::string out_dir = "csr_output";
    make_dir(out_dir);

    {
        std::ofstream f(out_dir + "/row_ptr.txt");
        for (int x : row_ptr) f << x << "\n";
    }
    {
        std::ofstream f(out_dir + "/col_idx.txt");
        for (int x : col_idx) f << x << "\n";
    }
    {
        std::ofstream f(out_dir + "/val.txt");
        f.precision(6);
        f << std::fixed;
        for (double x : val) f << x << "\n";
    }
    {
        std::ofstream f(out_dir + "/H.txt");
        f.precision(6);
        f << std::fixed;
        for (auto& row : H) {
            for (size_t j = 0; j < row.size(); j++) {
                f << row[j];
                if (j + 1 < row.size()) f << " ";
            }
            f << "\n";
        }
    }
    {
        std::ofstream f(out_dir + "/expected_out.txt");
        f.precision(6);
        f << std::fixed;
        for (auto& row : Z) {
            for (size_t j = 0; j < row.size(); j++) {
                f << row[j];
                if (j + 1 < row.size()) f << " ";
            }
            f << "\n";
        }
    }

    std::cout << "\nWrote CSR files to " << out_dir << "/:\n"
              << "  A is " << a_rows << "x" << a_cols
              << ", non-zero entries (nnz) = " << val.size() << "\n"
              << "  H is " << h_rows << "x" << h_cols << "\n"
              << "  row_ptr.txt, col_idx.txt, val.txt, H.txt, expected_out.txt\n";

    return 0;
}