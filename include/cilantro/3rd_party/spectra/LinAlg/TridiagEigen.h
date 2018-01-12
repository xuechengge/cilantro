// The code was adapted from Eigen/src/Eigenvaleus/SelfAdjointEigenSolver.h
//
// Copyright (C) 2008-2010 Gael Guennebaud <gael.guennebaud@inria.fr>
// Copyright (C) 2010 Jitse Niesen <jitse@maths.leeds.ac.uk>
// Copyright (C) 2016-2017 Yixuan Qiu <yixuan.qiu@cos.name>
//
// This Source Code Form is subject to the terms of the Mozilla
// Public License v. 2.0. If a copy of the MPL was not distributed
// with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef TRIDIAG_EIGEN_H
#define TRIDIAG_EIGEN_H

#include <Eigen/Core>
#include <Eigen/Jacobi>
#include <stdexcept>

namespace Spectra {


template <typename Scalar = double>
class TridiagEigen
{
private:
    // For convenience in adapting the tridiagonal_qr_step() function
    typedef Scalar RealScalar;

    typedef Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic> Matrix;
    typedef Eigen::Matrix<Scalar, Eigen::Dynamic, 1> Vector;

    typedef typename Matrix::Index Index;

    typedef Eigen::Ref<Matrix> GenericMatrix;
    typedef const Eigen::Ref<const Matrix> ConstGenericMatrix;

    Index m_n;
    Vector m_main_diag;     // Main diagonal elements of the matrix
    Vector m_sub_diag;      // Sub-diagonal elements of the matrix
    Matrix m_evecs;         // To store eigenvectors

    bool m_computed;

    // Adapted from Eigen/src/Eigenvaleus/SelfAdjointEigenSolver.h
    static void tridiagonal_qr_step(RealScalar* diag,
                                    RealScalar* subdiag, Index start,
                                    Index end, Scalar* matrixQ,
                                    Index n)
    {
        using std::abs;

        RealScalar td = (diag[end-1] - diag[end]) * RealScalar(0.5);
        RealScalar e = subdiag[end-1];
        // Note that thanks to scaling, e^2 or td^2 cannot overflow, however they can still
        // underflow thus leading to inf/NaN values when using the following commented code:
        //   RealScalar e2 = numext::abs2(subdiag[end-1]);
        //   RealScalar mu = diag[end] - e2 / (td + (td>0 ? 1 : -1) * sqrt(td*td + e2));
        // This explain the following, somewhat more complicated, version:
        RealScalar mu = diag[end];
        if(td == Scalar(0))
            mu -= abs(e);
        else
        {
            RealScalar e2 = Eigen::numext::abs2(subdiag[end-1]);
            RealScalar h = Eigen::numext::hypot(td, e);
            if(e2==RealScalar(0)) mu -= (e / (td + (td>RealScalar(0) ? RealScalar(1) : RealScalar(-1)))) * (e / h);
            else                  mu -= e2 / (td + (td>RealScalar(0) ? h : -h));
        }

        RealScalar x = diag[start] - mu;
        RealScalar z = subdiag[start];
        Eigen::Map<Matrix> q(matrixQ, n, n);
        for(Index k = start; k < end; ++k)
        {
            Eigen::JacobiRotation<RealScalar> rot;
            rot.makeGivens(x, z);

            const RealScalar s = rot.s();
            const RealScalar c = rot.c();

            // do T = G' T G
            RealScalar sdk = s * diag[k] + c * subdiag[k];
            RealScalar dkp1 = s * subdiag[k] + c * diag[k + 1];

            diag[k] = c * (c * diag[k] - s * subdiag[k]) - s * (c * subdiag[k] - s * diag[k + 1]);
            diag[k + 1] = s * sdk + c * dkp1;
            subdiag[k] = c * sdk - s * dkp1;

            if(k > start)
                subdiag[k - 1] = c * subdiag[k - 1] - s * z;

            x = subdiag[k];

            if(k < end - 1)
            {
                z = -s * subdiag[k+1];
                subdiag[k + 1] = c * subdiag[k + 1];
            }

            // apply the givens rotation to the unit matrix Q = Q * G
            if(matrixQ)
                q.applyOnTheRight(k, k + 1, rot);
        }
    }

public:
    TridiagEigen() :
        m_n(0), m_computed(false)
    {}

    TridiagEigen(ConstGenericMatrix &mat) :
        m_n(mat.rows()), m_computed(false)
    {
        compute(mat);
    }

    void compute(ConstGenericMatrix &mat)
    {
        using std::abs;

        if(mat.rows() != mat.cols())
            throw std::invalid_argument("TridiagEigen: matrix must be square");

        m_n = mat.rows();
        m_main_diag = mat.diagonal();
        m_sub_diag = mat.diagonal(-1);
        m_evecs.resize(m_n, m_n);
        m_evecs.setIdentity();

        // Scale matrix to improve stability
        const Scalar scale = std::max(m_main_diag.cwiseAbs().maxCoeff(),
                                      m_sub_diag.cwiseAbs().maxCoeff());
        m_main_diag /= scale;
        m_sub_diag /= scale;

        Scalar* diag = m_main_diag.data();
        Scalar* subdiag = m_sub_diag.data();

        Index end = m_n - 1;
        Index start = 0;
        int iter = 0; // total number of iterations
        int info = 0; // 0 for success, 1 for failure

        const Scalar eps = Eigen::NumTraits<Scalar>::epsilon();
        // Ideally this should be std::numeric_limits<Scalar>::min() as in Eigen,
        // but to allow for other float types, here we use eps^2
        const Scalar considerAsZero = eps * eps;
        const Scalar precision = Scalar(2) * eps;

        while(end > 0)
        {
            for(Index i = start; i < end; i++)
                if(abs(subdiag[i]) <= considerAsZero ||
                   abs(subdiag[i]) <= (abs(diag[i]) + abs(diag[i + 1])) * precision)
                    subdiag[i] = 0;

            // find the largest unreduced block
            while(end > 0 && subdiag[end - 1] == Scalar(0))
                end--;

            if(end <= 0)
                break;

            // if we spent too many iterations, we give up
            iter++;
            if(iter > 30 * m_n)
            {
                info = 1;
                break;
            }

            start = end - 1;
            while(start > 0 && subdiag[start - 1] != Scalar(0))
                start--;

            tridiagonal_qr_step(diag, subdiag, start, end, m_evecs.data(), m_n);
        }

        if(info > 0)
            throw std::runtime_error("TridiagEigen: eigen decomposition failed");

        // Scale eigenvalues back
        m_main_diag *= scale;

        m_computed = true;
    }

    Vector eigenvalues()
    {
        if(!m_computed)
            throw std::logic_error("TridiagEigen: need to call compute() first");

        // After calling compute(), main_diag will contain the eigenvalues.
        return m_main_diag;
    }

    Matrix eigenvectors()
    {
        if(!m_computed)
            throw std::logic_error("TridiagEigen: need to call compute() first");

        return m_evecs;
    }
};


} // namespace Spectra

#endif // TRIDIAG_EIGEN_H
