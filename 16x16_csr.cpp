// create_csr.cpp -- convert an edge-list graph into CSR files for the
// SpMV engine (row_ptr.txt, col_idx.txt, val.txt), plus a random dense
// feature matrix H.txt and an independently computed expected_out.txt.
// Pure C++ standard library, no external dependencies (no numpy/scipy).
//
// Input edge-list format, one edge per line:
//     src dst [weight]
// If weight is omitted, every edge gets weight 1.0. Lines starting with
// '#' and blank lines are skipped.
//
// Usage:
//   ./create_csr edges.txt output_dir [--feature-dim N] [--undirected]
//                [--self-loops] [--gcn-normalize] [--dense] [--feature-file F] [--seed N]

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <random>

#ifdef _WIN32
    #include <direct.h>
#else
    #include <sys/stat.h>
#endif

struct Edge {
    int src, dst;
    double weight;
};

static std::vector<Edge> read_edge_list(const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        std::cerr << "Error: cannot open " << path << "\n";
        exit(1);
    }
    std::vector<Edge> edges;
    std::string line;
    while (std::getline(f, line)) {
        size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        std::istringstream iss(line);
        int src, dst;
        double w = 1.0;
        if (!(iss >> src >> dst)) continue;
        iss >> w;
        edges.push_back({src, dst, w});
    }
    return edges;
}

static std::vector<Edge> read_dense_matrix(const std::string& path, int& num_rows_out) {
    std::ifstream f(path);
    if (!f) {
        std::cerr << "Error: cannot open " << path << "\n";
        exit(1);
    }
    std::vector<Edge> edges;
    std::string line;
    int row = 0;
    while (std::getline(f, line)) {
        size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        std::istringstream iss(line);
        double val;
        int col = 0;
        bool any_value = false;
        while (iss >> val) {
            any_value = true;
            if (val != 0.0) edges.push_back({row, col, val});
            col++;
        }
        if (any_value) row++;
    }
    num_rows_out = row;
    return edges;
}

static void make_dir(const std::string& path) {
#ifdef _WIN32
    _mkdir(path.c_str());
#else
    mkdir(path.c_str(), 0755);
#endif
}

static std::vector<std::vector<double>> read_feature_file(const std::string& path,
                                                            int num_nodes, int& feature_dim_out) {
    std::ifstream f(path);
    if (!f) {
        std::cerr << "Error: cannot open feature file " << path << "\n";
        exit(1);
    }
    std::vector<std::vector<double>> H;
    std::string line;
    while (std::getline(f, line)) {
        size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        std::istringstream iss(line);
        std::vector<double> row;
        double v;
        while (iss >> v) row.push_back(v);
        if (!row.empty()) H.push_back(row);
    }
    if ((int)H.size() != num_nodes) {
        std::cerr << "Error: feature file has " << H.size() << " rows, but matrix has "
                  << num_nodes << " nodes -- these must match.\n";
        exit(1);
    }
    feature_dim_out = (int)H[0].size();
    for (auto& row : H) {
        if ((int)row.size() != feature_dim_out) {
            std::cerr << "Error: feature file rows have inconsistent widths.\n";
            exit(1);
        }
    }
    return H;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: " << argv[0]
                  << " <edge_list> <out_dir> [--feature-dim N] [--undirected]"
                     " [--self-loops] [--gcn-normalize] [--dense] [--feature-file F] [--seed N]\n";
        return 1;
    }

    std::string edge_path = argv[1];
    std::string out_dir   = argv[2];

    int feature_dim = 32;
    bool undirected = false, self_loops = false, gcn_normalize = false, dense_input = false;
    unsigned seed = 0;
    std::string feature_file = "";

    for (int i = 3; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--feature-dim" && i + 1 < argc) feature_dim = std::stoi(argv[++i]);
        else if (arg == "--undirected") undirected = true;
        else if (arg == "--self-loops") self_loops = true;
        else if (arg == "--gcn-normalize") gcn_normalize = true;
        else if (arg == "--dense") dense_input = true;
        else if (arg == "--feature-file" && i + 1 < argc) feature_file = argv[++i];
        else if (arg == "--seed" && i + 1 < argc) seed = std::stoul(argv[++i]);
    }

    int dense_num_rows = 0;
    std::vector<Edge> edges = dense_input ? read_dense_matrix(edge_path, dense_num_rows)
                                           : read_edge_list(edge_path);
    if (edges.empty()) {
        std::cerr << "Error: no edges found in " << edge_path << "\n";
        return 1;
    }

    int num_nodes = dense_input ? dense_num_rows : 0;
    if (!dense_input) {
        for (auto& e : edges) num_nodes = std::max({num_nodes, e.src + 1, e.dst + 1});
    }

    if (undirected) {
        size_t n = edges.size();
        for (size_t i = 0; i < n; i++) {
            edges.push_back({edges[i].dst, edges[i].src, edges[i].weight});
        }
    }

    if (self_loops) {
        for (int i = 0; i < num_nodes; i++) edges.push_back({i, i, 1.0});
    }

    std::sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b) {
        if (a.src != b.src) return a.src < b.src;
        return a.dst < b.dst;
    });

    std::vector<Edge> merged;
    for (auto& e : edges) {
        if (!merged.empty() && merged.back().src == e.src && merged.back().dst == e.dst) {
            merged.back().weight += e.weight;
        } else {
            merged.push_back(e);
        }
    }
    edges = merged;

    if (gcn_normalize) {
        std::vector<double> deg(num_nodes, 0.0);
        for (auto& e : edges) deg[e.src] += e.weight;
        for (int i = 0; i < num_nodes; i++) if (deg[i] == 0.0) deg[i] = 1.0;
        for (auto& e : edges) {
            e.weight *= 1.0 / std::sqrt(deg[e.src]) * 1.0 / std::sqrt(deg[e.dst]);
        }
    }

    std::vector<int> row_ptr(num_nodes + 1, 0);
    std::vector<int> col_idx(edges.size());
    std::vector<double> val(edges.size());

    for (size_t k = 0; k < edges.size(); k++) {
        col_idx[k] = edges[k].dst;
        val[k] = edges[k].weight;
        row_ptr[edges[k].src + 1]++;
    }
    for (int i = 0; i < num_nodes; i++) row_ptr[i + 1] += row_ptr[i];

    std::vector<std::vector<double>> H;
    if (!feature_file.empty()) {
        H = read_feature_file(feature_file, num_nodes, feature_dim);
    } else {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<double> dist(-1.0, 1.0);
        H.assign(num_nodes, std::vector<double>(feature_dim));
        for (int i = 0; i < num_nodes; i++)
            for (int f = 0; f < feature_dim; f++)
                H[i][f] = dist(rng);
    }

    std::vector<std::vector<double>> Z(num_nodes, std::vector<double>(feature_dim, 0.0));
    for (int i = 0; i < num_nodes; i++) {
        for (int k = row_ptr[i]; k < row_ptr[i + 1]; k++) {
            int j = col_idx[k];
            double v = val[k];
            for (int f = 0; f < feature_dim; f++) Z[i][f] += v * H[j][f];
        }
    }

    make_dir(out_dir);

    auto write_int_vec = [&](const std::string& name, const std::vector<int>& v) {
        std::ofstream f(out_dir + "/" + name);
        for (int x : v) f << x << "\n";
    };
    auto write_dbl_vec = [&](const std::string& name, const std::vector<double>& v) {
        std::ofstream f(out_dir + "/" + name);
        f.precision(6);
        f << std::fixed;
        for (double x : v) f << x << "\n";
    };
    auto write_matrix = [&](const std::string& name, const std::vector<std::vector<double>>& M) {
        std::ofstream f(out_dir + "/" + name);
        f.precision(6);
        f << std::fixed;
        for (auto& row : M) {
            for (size_t j = 0; j < row.size(); j++) {
                f << row[j];
                if (j + 1 < row.size()) f << " ";
            }
            f << "\n";
        }
    };

    write_int_vec("row_ptr.txt", row_ptr);
    write_int_vec("col_idx.txt", col_idx);
    write_dbl_vec("val.txt", val);
    write_matrix("H.txt", H);
    write_matrix("expected_out.txt", Z);

    std::cout << "Wrote CSR files to " << out_dir << "/:\n"
              << "  nodes = " << num_nodes << ", nnz = " << edges.size()
              << ", feature_dim = " << feature_dim << "\n"
              << "  row_ptr.txt, col_idx.txt, val.txt, H.txt, expected_out.txt\n";

    return 0;
}