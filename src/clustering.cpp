#include "clustering.hpp"

#include "constant_series.hpp"
#include "monomials.hpp"
#include "nchoosek.hpp"
#include "p_max_total.hpp"


namespace fgt {


namespace {


double ddist(const arma::rowvec& x, const arma::rowvec& y) {
    return arma::accu(arma::pow(x - y, 2));
}
}


Clustering::Clustering(const arma::mat& source, int K, double bandwidth,
                       double epsilon)
    : m_source(source),
      m_indices(arma::zeros<arma::uvec>(source.n_rows)),
      m_centers(arma::zeros<arma::mat>(K, source.n_cols)),
      m_num_points(arma::zeros<arma::uvec>(K)),
      m_radii(K),
      m_rx(),
      m_bandwidth(bandwidth),
      m_epsilon(epsilon),
      m_p_max(0),
      m_constant_series() {
    m_p_max =
        choose_truncation_number(m_source.n_cols, m_bandwidth, m_epsilon, m_rx);
    m_constant_series = compute_constant_series(m_source.n_cols, m_p_max);
}


arma::uword Clustering::choose_truncation_number(int dimensions, double bandwidth,
                                                 double epsilon, double rx) {
    double r = std::min(std::sqrt(dimensions),
                        bandwidth * std::sqrt(std::log(1 / epsilon)));
    double rx2 = rx * rx;
    double h2 = bandwidth * bandwidth;
    double error = 1;
    double temp = 1;
    int p = 0;

    while ((error > epsilon) and (p <= TruncationNumberUpperLimit)) {
        ++p;
        double b = std::min((rx + std::sqrt(rx2 + 2 * p * h2)) / 2, rx + r);
        double c = rx - b;
        temp *= 2 * rx * b / h2 / p;
        error = temp * std::exp(-c * c / h2);
    }

    return p;
}


arma::mat Clustering::compute_C(const arma::vec& q) const {
    arma::mat C = arma::zeros<arma::mat>(
        m_centers.n_rows, get_p_max_total(m_source.n_cols, m_p_max));
    double h2 = m_bandwidth * m_bandwidth;

    for (arma::uword i = 0; i < m_source.n_rows; ++i) {
        arma::uword k = m_indices(i);
        arma::rowvec dx = m_source.row(i) - m_centers.row(k);
        double distance2 = arma::accu(arma::pow(dx, 2));
        arma::rowvec center_monomials =
            compute_monomials(dx / m_bandwidth, m_p_max);
        double f = q(i) * std::exp(-distance2 / h2);
        C.row(k) += f * center_monomials;
    }

    arma::rowvec constant_series =
        compute_constant_series(m_source.n_cols, m_p_max);
    for (arma::uword i = 0; i < C.n_rows; ++i) {
        C.row(i) = C.row(i) % constant_series;
    }

    return C;
}


const optional_arma_uword_t GonzalezClustering::DefaultStartingIndex = {false,
                                                                        0};


GonzalezClustering::GonzalezClustering(const arma::mat& source, int K,
                                       double bandwidth, double epsilon,
                                       optional_arma_uword_t starting_index)
    : Clustering(source, K, bandwidth, epsilon) {
    arma::uword N = source.n_rows;
    arma::uvec centers(K);
    arma::uvec cprev(N);
    arma::uvec cnext(N);
    arma::uvec far2c(K);
    arma::vec dist(N);

    arma::uword nc;
    if (starting_index.first) {
        nc = starting_index.second;
    } else {
        std::random_device rd;
        nc = rd() % N;
    }
    centers(0) = nc;

    for (arma::uword i = 0; i < N; ++i) {
        dist(i) = (i == nc) ? 0.0 : ddist(source.row(i), source.row(nc));
        cnext(i) = i + 1;
        cprev(i) = i - 1;
    }

    cnext(N - 1) = 0;
    cprev(0) = N - 1;

    nc = std::max_element(dist.begin(), dist.end()) - dist.begin();
    far2c(0) = nc;
    set_radius(0, dist(nc));

    for (int i = 1; i < K; ++i) {
        nc = far2c(get_radius_idxmax(i));

        centers(i) = nc;
        set_radius(i, 0.0);
        dist(nc) = 0.0;
        set_index(nc, i);
        far2c(i) = nc;

        cnext(cprev(nc)) = cnext(nc);
        cprev(cnext(nc)) = cprev(nc);
        cnext(nc) = nc;
        cprev(nc) = nc;

        for (int j = 0; j < i; ++j) {
            arma::uword ct_j = centers(j);
            double dc2cq = ddist(source.row(ct_j), source.row(nc)) / 4;
            if (dc2cq < get_radius(j)) {
                set_radius(j, 0.0);
                far2c(j) = ct_j;
                arma::uword k = cnext(ct_j);
                while (k != ct_j) {
                    arma::uword nextk = cnext(k);
                    double dist2c_k = dist(k);
                    if (dc2cq < dist2c_k) {
                        double dd = ddist(source.row(k), source.row(nc));
                        if (dd < dist2c_k) {
                            dist(k) = dd;
                            set_index(k, i);
                            if (get_radius(i) < dd) {
                                set_radius(i, dd);
                                far2c(i) = k;
                            }
                            cnext(cprev(k)) = nextk;
                            cprev(nextk) = cprev(k);
                            cnext(k) = cnext(nc);
                            cprev(cnext(nc)) = k;
                            cnext(nc) = k;
                            cprev(k) = nc;
                        } else if (get_radius(j) < dist2c_k) {
                            set_radius(j, dist2c_k);
                            far2c(j) = k;
                        }
                    } else if (get_radius(j) < dist2c_k) {
                        set_radius(j, dist2c_k);
                        far2c(j) = k;
                    }
                    k = nextk;
                }
            }
        }
    }

    set_radii(arma::sqrt(get_radii()));
    set_rx(get_max_radius());

    arma::mat center_coordinates(get_centers());
    for (arma::uword i = 0; i < N; ++i) {
        increment_num_points(get_index(i));
        center_coordinates.row(get_index(i)) += source.row(i);
    }
    set_centers(center_coordinates /
                arma::repmat(get_num_points(), 1, get_d()));
}
}
