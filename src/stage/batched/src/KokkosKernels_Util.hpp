#ifndef __KOKKOSKERNELS_UTIL_HPP__
#define __KOKKOSKERNELS_UTIL_HPP__

/// \author Kyungjoo Kim (kyukim@sandia.gov)


#include <iomanip>
#include <random>
#include <string>

#include <limits>
#include <cmath>
#include <ctime>

#include <complex>
#include "Kokkos_Complex.hpp"

namespace KokkosKernels {
  namespace Batched {
    namespace Experimental {

#define Int2StringHelper(A) #A
#define Int2String(A) Int2StringHelper(A)
#define StringCat(A,B) A B

      // to use double, std::complex<double>, Kokkos::complex<double>
      using std::abs;

      // view manipulation
      template <typename MemoryTraitsType, Kokkos::MemoryTraitsFlags flag>
      using MemoryTraits = Kokkos::MemoryTraits<MemoryTraitsType::Unmanaged |
                                                MemoryTraitsType::RandomAccess |
                                                //  MemoryTraitsType::Atomic |
                                                flag>;

      template <typename ViewType>
      using UnmanagedViewType 
      = Kokkos::View<typename ViewType::data_type,
                     typename ViewType::array_layout,
                     typename ViewType::device_type,
                     MemoryTraits<typename ViewType::memory_traits, Kokkos::Unmanaged> >;

      template <typename ViewType>
      using ConstViewType = Kokkos::View<typename ViewType::const_data_type,
                                         typename ViewType::array_layout,
                                         typename ViewType::device_type,
                                         typename ViewType::memory_traits>;
      template <typename ViewType>
      using ConstUnmanagedViewType = ConstViewType<UnmanagedViewType<ViewType> >;

      // helper for vector type 
      template<typename T>
      KOKKOS_INLINE_FUNCTION      
      typename std::enable_if<std::is_fundamental<T>::value,size_t>::type 
      adjustDimension(const size_t &m) {
        return m;
      }

      template<typename T>
      KOKKOS_INLINE_FUNCTION      
      typename std::enable_if<!std::is_fundamental<T>::value,size_t>::type 
      adjustDimension(const size_t &m) {
        return (m/T::vector_length + (m%T::vector_length > 0));
      }

      template<size_t BufSize, typename SpaceType = Kokkos::DefaultExecutionSpace>
      struct Flush {
        typedef double value_type;
    
        // flush a large host buffer 
        Kokkos::View<value_type*,SpaceType> _buf;
        Flush() : _buf("Flush::buf", BufSize/sizeof(double)) {
          Kokkos::deep_copy(_buf, 1);       
        }

        KOKKOS_INLINE_FUNCTION    
        void init(value_type &update) {
          update = 0;
        }
    
        KOKKOS_INLINE_FUNCTION
        void join(volatile value_type &update,
                  const volatile value_type &input) {
          update += input; 
        }

        KOKKOS_INLINE_FUNCTION
        void operator()(const int i, value_type &update) const {
          update += _buf[i];
        }

        void run() {
          double sum = 0;
          Kokkos::parallel_reduce(Kokkos::RangePolicy<SpaceType>(0,BufSize/sizeof(double)), *this, sum);
          SpaceType::fence();
          FILE *fp = fopen("/dev/null", "w");
          fprintf(fp, "%f\n", sum);
          fclose(fp);
        }

      };

      template<typename ValueType>
      struct Random;
      
      template<>
      struct Random<double> {
        Random(const unsigned int seed = 0) { srand(seed); }
        double value() { return rand()/((double) RAND_MAX + 1.0); }
      };

      template<>
      struct Random<std::complex<double> > {
        Random(const unsigned int seed = 0) { srand(seed); }
        std::complex<double> value() { 
          return std::complex<double>(rand()/((double) RAND_MAX + 1.0),
                                      rand()/((double) RAND_MAX + 1.0)); 
        }
      };

      template<>
      struct Random<Kokkos::complex<double> > {
        Random(const unsigned int seed = 0) { srand(seed); }
        Kokkos::complex<double> value() { 
          return Kokkos::complex<double>(rand()/((double) RAND_MAX + 1.0),
                                         rand()/((double) RAND_MAX + 1.0)); 
        }
      };

      struct Timer {
        std::string _label;
        Kokkos::Impl::Timer _clock;
        Timer (const std::string label)
          : _label(label), _clock() {};
    
        void reset() { _clock.reset(); }
        double seconds() { return _clock.seconds(); }
        ~Timer() {
          Kokkos::fence();
          const double t = _clock.seconds();
          std::string label = _label; label.resize(24);
          std::cout << "KokkosKernels::Timer:: " << std::setw(26) << label
                    << std::setw(15) << std::scientific << t << " [sec] " << std::endl;
        }
      };
  
      // Implicit vectorization
      template<typename T, 
               typename SpT = Kokkos::DefaultHostExecutionSpace>
      struct SIMD { 
        static_assert( std::is_same<T,double>::value                   ||
                       std::is_same<T,float>::value                    ||
                       std::is_same<T,Kokkos::complex<double> >::value || 
                       std::is_same<T,std::complex<double> >::value,
                       "KokkosKernels:: Invalid SIMD<> type." );

        static_assert( std::is_same<SpT,Kokkos::Serial>::value ||
                       std::is_same<SpT,Kokkos::OpenMP>::value,
                       "KokkosKernels:: Invalid SIMD<> exec space." );

        using value_type = T; 
        using exec_space = SpT;
      };
  
      // Intel AVX instruction device (explicit vectorization)
      template<typename T,
               typename SpT = Kokkos::DefaultHostExecutionSpace>
      struct AVX {
        static_assert( std::is_same<T,double>::value                   ||
                       std::is_same<T,float>::value                    ||
                       std::is_same<T,Kokkos::complex<double> >::value || 
                       std::is_same<T,std::complex<double> >::value,
                       "KokkosKernels:: Invalid AVX<> type." );

        static_assert( std::is_same<SpT,Kokkos::Serial>::value ||
                       std::is_same<SpT,Kokkos::OpenMP>::value,
                       "KokkosKernels:: Invalid AVX<> exec space." );

        using value_type = T;
        using exec_space = SpT;
      };

//       // Cuda threading (explicit thread vectorization)
//       template<typename T, 
//                typename SpT = Kokkos::DefaultExecutionSpace>
//       struct SIMT { 
//         static_assert( std::is_same<T,double>::value ||
//                        std::is_same<T,float>::value,
//                        "KokkosKernels:: Invalid SIMT<> type." );

//         static_assert( !std::is_same<SpT,Kokkos::Serial>::value &&
//                        !std::is_same<SpT,Kokkos::OpenMP>::value,
//                        "KokkosKernels:: Invalid AVX<> exec space." );

// #if defined(KOKKOS_HAVE_CUDA)
//         static_assert( std::is_same<SpT,Kokkos::Cuda>::value,
//                        "KokkosKernels:: Invalid SIMT<> exec space." );
// #endif

//         using value_type = T; 
//         using exec_space = SpT;
//       };
  
      template<class T, int l>
      struct VectorTag {
        using value_type = typename T::value_type;
        using exec_space = typename T::exec_space;
        using member_type = typename Kokkos::Impl::TeamPolicyInternal<exec_space>::member_type;
    
        static_assert( std::is_same<T,SIMD<value_type,exec_space> >::value || // host compiler vectorization
                       std::is_same<T,AVX<value_type, exec_space> >::value, // || // host AVX vectorization
                       // std::is_same<T,SIMT<value_type,exec_space> >::value,   // cuda thread vectorization
                       "KokkosKernels:: Invalid VectorUnitTag<> type." );
    
        using type = VectorTag;
        enum : int { length = l };
      };
  

      // Tags for BLAS
      struct Trans {
        struct Transpose {};
        struct NoTranspose {};
        struct ConjTranspose {};
      };

      struct Side {
        struct Left {};
        struct Right {};
      };

      struct Uplo {
        struct Upper {};
        struct Lower {};
      };
  
      struct Diag {
        struct Unit    { enum : bool { use_unit_diag = true }; };
        struct NonUnit { enum : bool { use_unit_diag = false}; };
      };

      struct Algo {
        struct Gemm { 
          struct Unblocked {};
          struct Blocked {
            /// TODO:: for now harwire the blocksizes; this should reflect 
            ///        regieter blocking (not about team parallelism).
            /// this mb should vary according to
            /// - team policy (smaller) or range policy (bigger)
            /// - space (cuda vs host)
            /// - blocksize input (blk <= 4 mb = 2, otherwise mb = 4), etc.
#if defined(KOKKOS_HAVE_CUDA)
            template<typename ActiveMemorySpaceType> KOKKOS_INLINE_FUNCTION static constexpr 
            typename std::enable_if<std::is_same<ActiveMemorySpaceType,Kokkos::CudaSpace>::value,int>
            ::type mb() { return 4; }
#endif
            template<typename ActiveMemorySpaceType> KOKKOS_INLINE_FUNCTION static constexpr 
            typename std::enable_if<std::is_same<ActiveMemorySpaceType,Kokkos::HostSpace>::value,int>
            ::type mb() { return 4; }  
          };
          struct MKL {};
          struct CompactMKL {};
        };

        struct Trsm {
          struct Unblocked {};
          struct Blocked {
#if defined(KOKKOS_HAVE_CUDA)
            template<typename ActiveMemorySpaceType> KOKKOS_INLINE_FUNCTION static constexpr 
            typename std::enable_if<std::is_same<ActiveMemorySpaceType,Kokkos::CudaSpace>::value,int>
            ::type mb() { return 4; }
#endif
            template<typename ActiveMemorySpaceType> KOKKOS_INLINE_FUNCTION static constexpr 
            typename std::enable_if<std::is_same<ActiveMemorySpaceType,Kokkos::HostSpace>::value,int>
            ::type mb() { return 4; }  
          };
          struct MKL {};
          struct CompactMKL {};
        };

        struct LU {
          struct Unblocked {};
          struct Blocked {
            enum : int { mb = 4 };
          };
          struct MKL {};
          struct CompactMKL {};
        };

        struct Gemv { 
          struct Unblocked {};
          struct Blocked {
            enum : int { mb = 4 };
          };
          struct MKL {};
          struct CompactMKL {};
        };

        struct Trsv { 
          struct Unblocked {};
          struct Blocked {
            enum : int { mb = 4 };
          };
          struct MKL {};
          struct CompactMKL {};
        };

      };

      struct Util {

        template<typename ValueType>
        KOKKOS_INLINE_FUNCTION
        static void
        packColMajor(ValueType *__restrict__ A, 
                     const int m, 
                     const int n,
                     const ValueType *__restrict__ B,
                     const int bs0,
                     const int bs1) {
          for (int j=0;j<n;++j) 
            for (int i=0;i<m;++i)
              A[i+j*m] = B[i*bs0+j*bs1];
        }

        template<typename ValueType>
        KOKKOS_INLINE_FUNCTION
        static void
        packRowMajor(ValueType *__restrict__ A, 
                     const int m, 
                     const int n,
                     const ValueType *__restrict__ B,
                     const int bs0,
                     const int bs1) {
          for (int i=0;i<m;++i)
            for (int j=0;j<n;++j) 
              A[i*n+j] = B[i*bs0+j*bs1];
        }

      };

    }
  }
}
#endif
