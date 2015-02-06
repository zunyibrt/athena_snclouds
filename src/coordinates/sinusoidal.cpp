// Minkowski spacetime, sinusoidal ("snake") coordinates
// Notes:
//   coordinates: t, x, y, z
//   parameters: a, k
//   metric:
//     ds^2 = -dt^2 + \alpha^2 dx^2 - 2 \beta dx dy + dy^2 + dz^2
//     alpha = \sqrt(1 + a^2 k^2 \cos^2(k x))
//     \beta = a k \cos(k x)
//   relation to Minkowski (Cartesian) coordinates:
//     t = t_m
//     x = x_m
//     y = y_m + a \sin(k x_m)
//     z = z_m

// Primary header
#include "coordinates.hpp"

// C++ headers
#include <cmath>  // cos(), sin(), sqrt()

// Athena headers
#include "../athena.hpp"         // enums, macros, Real
#include "../athena_arrays.hpp"  // AthenaArray
#include "../fluid/eos/eos.hpp"  // GetGamma()
#include "../fluid/fluid.hpp"    // Fluid
#include "../mesh.hpp"           // MeshBlock

// TODO: find better input method
namespace globals
{
  const Real A = 10.0;
  const Real K = 0.1;
};
using namespace globals;

//--------------------------------------------------------------------------------------

// Constructor
// Inputs:
//   pb: pointer to block containing this grid
//   pin: pointer to runtime inputs
Coordinates::Coordinates(MeshBlock *pb, ParameterInput *pin)
{
  // Set pointer to host MeshBlock
  pmy_block = pb;

  // Initialize volume-averaged positions and spacings: x-direction
  for (int i = pb->is-NGHOST; i <= pb->ie+NGHOST; ++i)
    pb->x1v(i) = 0.5 * (pb->x1f(i) + pb->x1f(i+1));
  for (int i = pb->is-NGHOST; i <= pb->ie+NGHOST-1; ++i)
    pb->dx1v(i) = pb->x1v(i+1) - pb->x1v(i);

  // Initialize volume-averaged positions and spacings: y-direction
  if (pb->block_size.nx2 == 1)  // no extent
  {
    pb->x2v(pb->js) = 0.5 * (pb->x2f(pb->js) + pb->x2f(pb->js+1));
    pb->dx2v(pb->js) = pb->dx2f(pb->js);
  }
  else  // extended
  {
    for (int j = pb->js-NGHOST; j <= pb->je+NGHOST; ++j)
      pb->x2v(j) = 0.5 * (pb->x2f(j) + pb->x2f(j+1));
    for (int j = pb->js-NGHOST; j <= pb->je+NGHOST-1; ++j)
      pb->dx2v(j) = pb->x2v(j+1) - pb->x2v(j);
  }

  // Initialize volume-averaged positions and spacings: z-direction
  if (pb->block_size.nx3 == 1)  // no extent
  {
    pb->x3v(pb->ks) = 0.5 * (pb->x3f(pb->ks) + pb->x3f(pb->ks+1));
    pb->dx3v(pb->ks) = pb->dx3f(pb->ks);
  }
  else  // extended
  {
    for (int k = pb->ks-NGHOST; k <= pb->ke+NGHOST; ++k)
      pb->x3v(k) = 0.5 * (pb->x3f(k) + pb->x3f(k+1));
    for (int k = pb->ks-NGHOST; k <= pb->ke+NGHOST-1; ++k)
      pb->dx3v(k) = pb->x3v(k+1) - pb->x3v(k);
  }

  // Allocate arrays for intermediate geometric quantities: x-direction
  int n_cells_1 = pb->block_size.nx1 + 2*NGHOST;
  cell_width1_i_.NewAthenaArray(n_cells_1);
  src_terms_i1_.NewAthenaArray(n_cells_1);
  metric_cell_i1_.NewAthenaArray(n_cells_1);
  metric_cell_i2_.NewAthenaArray(n_cells_1);
  metric_face1_i1_.NewAthenaArray(n_cells_1);
  metric_face1_i2_.NewAthenaArray(n_cells_1);
  metric_face2_i1_.NewAthenaArray(n_cells_1);
  metric_face2_i2_.NewAthenaArray(n_cells_1);
  metric_face3_i1_.NewAthenaArray(n_cells_1);
  metric_face3_i2_.NewAthenaArray(n_cells_1);
  trans_face1_i2_.NewAthenaArray(n_cells_1);
  trans_face2_i1_.NewAthenaArray(n_cells_1);
  trans_face2_i2_.NewAthenaArray(n_cells_1);
  trans_face3_i2_.NewAthenaArray(n_cells_1);

  // Calculate intermediate geometric quantities: r-direction
  #pragma simd
  for (int i = pb->is-NGHOST; i <= pb->ie+NGHOST; i++)
  {
    // Useful quantities
    Real r_c = pb->x1v(i);
    Real r_m = pb->x1f(i);
    Real r_p = pb->x1f(i+1);
    Real sin_2m = std::sin(2.0*K*r_m);
    Real sin_2p = std::sin(2.0*K*r_p);
    Real cos_c = std::cos(K*r_c);
    Real cos_m = std::cos(K*r_m);
    Real cos_p = std::cos(K*r_p);
    Real alpha_sq_c = 1.0 + SQR(A)*SQR(K) * SQR(cos_c);
    Real alpha_sq_m = 1.0 + SQR(A)*SQR(K) * SQR(cos_m);
    Real alpha_c = std::sqrt(alpha_sq_c);
    Real beta_c = A*K * cos_c;
    Real beta_m = A*K * cos_m;
    Real beta_p = A*K * cos_p;

    // Volumes, areas, lengths, and widths
    cell_width1_i_(i) = 1.0/(4.0*(1.0+SQR(A)*SQR(K)))
        * (2.0*K*(2.0+SQR(A)*SQR(K)) * pb->dx1f(i) - SQR(A)*SQR(K) * (sin_2m - sin_2p));

    // Source terms
    src_terms_i1_(i) = (beta_m - beta_p) / pb->dx1f(i);

    // Cell-centered metric
    metric_cell_i1_(i) = alpha_sq_c;
    metric_cell_i2_(i) = beta_c;

    // Face-centered metric
    metric_face1_i1_(i) = alpha_sq_m;
    metric_face1_i2_(i) = beta_m;
    metric_face2_i1_(i) = alpha_sq_c;
    metric_face2_i2_(i) = beta_c;
    metric_face3_i1_(i) = alpha_sq_c;
    metric_face3_i2_(i) = beta_c;

    // Coordinate transformations
    trans_face1_i2_(i) = beta_m;
    trans_face2_i1_(i) = alpha_c;
    trans_face2_i2_(i) = beta_c;
    trans_face3_i2_(i) = beta_m;
  }
}

//--------------------------------------------------------------------------------------

// Destructor
Coordinates::~Coordinates()
{
  cell_width1_i_.DeleteAthenaArray();
  src_terms_i1_.DeleteAthenaArray();
  metric_cell_i1_.DeleteAthenaArray();
  metric_cell_i2_.DeleteAthenaArray();
  metric_face1_i1_.DeleteAthenaArray();
  metric_face1_i2_.DeleteAthenaArray();
  metric_face2_i1_.DeleteAthenaArray();
  metric_face2_i2_.DeleteAthenaArray();
  metric_face3_i1_.DeleteAthenaArray();
  metric_face3_i2_.DeleteAthenaArray();
  trans_face1_i2_.DeleteAthenaArray();
  trans_face2_i1_.DeleteAthenaArray();
  trans_face2_i2_.DeleteAthenaArray();
  trans_face3_i2_.DeleteAthenaArray();
}

//--------------------------------------------------------------------------------------

// Function for computing cell volumes
// Inputs:
//   k: z-index
//   j: y-index
//   il,iu: x-index bounds
// Outputs:
//   volumes: 1D array of cell volumes
// Notes:
//   \Delta V = \Delta x * \Delta y * \Delta z
void Coordinates::CellVolume(const int k, const int j, const int il, const int iu,
    AthenaArray<Real> &volumes)
{
  const Real &delta_y = pmy_block->dx2f(j);
  const Real &delta_z = pmy_block->dx3f(k);
  #pragma simd
  for (int i = il; i <= iu; ++i)
  {
    const Real &delta_x = pmy_block->dx1f(i);
    Real &volume = volumes(i);
    volume = delta_x * delta_y * delta_z;
  }
  return;
}

//--------------------------------------------------------------------------------------

// Function for computing areas orthogonal to x
// Inputs:
//   k: z-index
//   j: y-index
//   il,iu: x-index bounds
// Outputs:
//   areas: 1D array of interface areas orthogonal to x
// Notes:
//   \Delta A = \Delta y * \Delta z
void Coordinates::Face1Area(const int k, const int j, const int il, const int iu,
    AthenaArray<Real> &areas)
{
  const Real &delta_y = pmy_block->dx2f(j);
  const Real &delta_z = pmy_block->dx3f(k);
  #pragma simd
  for (int i = il; i <= iu; ++i)
  {
    Real &area = areas(i);
    area = delta_y * delta_z;
  }
  return;
}

//--------------------------------------------------------------------------------------

// Function for computing areas orthogonal to y
// Inputs:
//   k: z-index
//   j: y-index
//   il,iu: x-index bounds
// Outputs:
//   areas: 1D array of interface areas orthogonal to y
// Notes:
//   \Delta A = \Delta x * \Delta z
void Coordinates::Face2Area(const int k, const int j, const int il, const int iu,
    AthenaArray<Real> &areas)
{
  const Real &delta_z = pmy_block->dx3f(k);
  #pragma simd
  for (int i = il; i <= iu; ++i)
  {
    const Real &delta_x = pmy_block->dx1f(i);
    Real &area = areas(i);
    area = delta_x * delta_z;
  }
  return;
}

//--------------------------------------------------------------------------------------

// Function for computing areas orthogonal to z
// Inputs:
//   k: z-index
//   j: y-index
//   il,iu: x-index bounds
// Outputs:
//   areas: 1D array of interface areas orthogonal to z
// Notes:
//   \Delta A = \Delta x * \Delta y
void Coordinates::Face3Area(const int k, const int j, const int il, const int iu,
    AthenaArray<Real> &areas)
{
  const Real &delta_y = pmy_block->dx2f(j);
  #pragma simd
  for (int i = il; i <= iu; ++i)
  {
    const Real &delta_x = pmy_block->dx1f(i);
    Real &area = areas(i);
    area = delta_x * delta_y;
  }
  return;
}

//--------------------------------------------------------------------------------------

// Function for computing lengths of edges in the x-direction
// Inputs:
//   k: z-index (unused)
//   j: y-index (unused)
//   il,iu: x-index bounds
// Outputs:
//   len: 1D array of edge lengths along x
// Notes:
//   \Delta L = \Delta x
void Coordinates::Edge1Length(const int k, const int j, const int il, const int iu,
    AthenaArray<Real> &len)
{
  #pragma simd
  for (int i = il; i <= iu; ++i)
    len(i) = pmy_block->dx1f(i);
  return;
}

//--------------------------------------------------------------------------------------

// Function for computing lengths of edges in the y-direction
// Inputs:
//   k: z-index (unused)
//   j: y-index
//   il,iu: x-index bounds
// Outputs:
//   len: 1D array of edge lengths along y
// Notes:
//   \Delta L = \Delta y
void Coordinates::Edge2Length(const int k, const int j, const int il, const int iu,
    AthenaArray<Real> &len)
{
  const Real &length = pmy_block->dx2f(j);
  #pragma simd
  for (int i = il; i <= iu; ++i)
    len(i) = length;
  return;
}

//--------------------------------------------------------------------------------------

// Function for computing lengths of edges in the z-direction
// Inputs:
//   k: z-index
//   j: y-index (unused)
//   il,iu: x-index bounds
// Outputs:
//   len: 1D array of edge lengths along z
// Notes:
//   \Delta L = \Delta z
void Coordinates::Edge3Length(const int k, const int j, const int il, const int iu,
    AthenaArray<Real> &len)
{
  const Real &length = pmy_block->dx3f(k);
  #pragma simd
  for (int i = il; i <= iu; ++i)
    len(i) = length;
  return;
}

//--------------------------------------------------------------------------------------

// Function for computing widths of cells in the x-direction
// Inputs:
//   k: z-index (unused)
//   j: y-index (unused)
//   i: x-index
// Outputs:
//   returned value: width of cell (i,j,k)
// Notes:
//   \Delta W <= \sqrt(1 + a^2 k^2) \Delta x
Real Coordinates::CenterWidth1(const int k, const int j, const int i)
{
  return cell_width1_i_(i);
}

//--------------------------------------------------------------------------------------

// Function for computing widths of cells in the y-direction
// Inputs:
//   k: z-index (unused)
//   j: y-index
//   i: x-index (unused)
// Outputs:
//   returned value: width of cell (i,j,k)
// Notes:
//   \Delta W = \Delta y
Real Coordinates::CenterWidth2(const int k, const int j, const int i)
{
  return pmy_block->dx2f(j);
}

//--------------------------------------------------------------------------------------

// Function for computing widths of cells in the z-direction
// Inputs:
//   k: z-index
//   j: y-index (unused)
//   i: x-index (unused)
// Outputs:
//   returned value: width of cell (i,j,k)
// Notes:
//   \Delta W = \Delta z
Real Coordinates::CenterWidth3(const int k, const int j, const int i)
{
  return pmy_block->dx3f(k);
}

//--------------------------------------------------------------------------------------

// Function for computing source terms
// Inputs:
//   dt: size of timestep
//   prim: full grid of primitive values at beginning of half timestep
//   cons: full grid of conserved variables at end of half timestep
// Outputs:
//   cons: source terms added
// Notes:
//   source terms all vanish identically
void Coordinates::CoordinateSourceTerms(Real dt, const AthenaArray<Real> &prim,
    AthenaArray<Real> &cons)
{
  // Extract ratio of specific heats
  const Real gamma_adi = pmy_block->pfluid->pf_eos->GetGamma();
  const Real gamma_adi_red = gamma_adi / (gamma_adi - 1.0);

  // Go through cells
  for (int k = pmy_block->ks; k <= pmy_block->ke; k++)
    for (int j = pmy_block->js; j <= pmy_block->je; j++)
    {
      #pragma simd
      for (int i = pmy_block->is; i <= pmy_block->ie; i++)
      {
        // Extract geometric quantities
        const Real g00 = -1.0;
        const Real &g11 = metric_cell_i1_(i);
        const Real &g12 = metric_cell_i2_(i);
        const Real g22 = 1.0;
        const Real g33 = 1.0;
        const Real &gamma_211 = src_terms_i1_(i);

        // Extract primitives
        const Real &rho = prim(IDN,k,j,i);
        const Real &pgas = prim(IEN,k,j,i);
        const Real &v1 = prim(IVX,k,j,i);
        const Real &v2 = prim(IVY,k,j,i);
        const Real &v3 = prim(IVZ,k,j,i);

        // Calculate 4-velocity
        Real u0 = std::sqrt(-1.0 /
            (g00 + g11*v1*v1 + 2.0*g12*v1*v2 + g22*v2*v2 + g33*v3*v3));
        Real u1 = u0 * v1;
        Real u2 = u0 * v2;
        Real u_2 = g12*u1 + g22*u2;

        // Calculate stress-energy tensor
        Real rho_h = rho + gamma_adi_red * pgas;
        Real t1_2 = rho_h * u1 * u_2;

        // Calculate source terms
        Real s1 = gamma_211 * t1_2;

        // Extract conserved quantities
        Real &m1 = cons(IM1,k,j,i);

        // Add source terms to conserved quantities
        m1 += dt * s1;
      }
    }
  return;
}

//--------------------------------------------------------------------------------------

// Function for computing cell-centered metric coefficients
// Inputs:
//   k: z-index
//   j: y-index
// Outputs:
//   g: array of metric components in 1D
//   g_inv: array of inverse metric components in 1D
void Coordinates::CellMetric(const int k, const int j, AthenaArray<Real> &g,
    AthenaArray<Real> &g_inv)
{
  #pragma simd
  for (int i = pmy_block->is-NGHOST; i <= pmy_block->ie+NGHOST; ++i)
  {
    // Extract geometric quantities
    const Real &alpha_sq = metric_cell_i1_(i);
    const Real &beta = metric_cell_i2_(i);

    // Extract metric terms
    Real &g00 = g(I00,i);
    Real &g11 = g(I11,i);
    Real &g12 = g(I12,i);
    Real &g22 = g(I22,i);
    Real &g33 = g(I33,i);
    Real &gi00 = g_inv(I00,i);
    Real &gi11 = g_inv(I11,i);
    Real &gi12 = g_inv(I12,i);
    Real &gi22 = g_inv(I22,i);
    Real &gi33 = g_inv(I33,i);

    // Set metric terms
    g00 = -1.0;
    g11 = alpha_sq;
    g12 = -beta;
    g22 = 1.0;
    g33 = 1.0;
    gi00 = -1.0;
    gi11 = 1.0;
    gi12 = beta;
    gi22 = alpha_sq;
    gi33 = 1.0;
  }
  return;
}

//--------------------------------------------------------------------------------------

// Function for computing face-centered metric coefficients: r-interface
// Inputs:
//   k: phi-index
//   j: theta-index
// Outputs:
//   g: array of metric components in 1D
//   g_inv: array of inverse metric components in 1D
void Coordinates::Face1Metric(const int k, const int j, AthenaArray<Real> &g,
    AthenaArray<Real> &g_inv)
{
  #pragma simd
  for (int i = pmy_block->is; i <= pmy_block->ie+1; i++)
  {
    // Extract geometric quantities
    const Real &alpha_sq = metric_face1_i1_(i);
    const Real &beta = metric_face1_i2_(i);

    // Extract metric terms
    Real &g00 = g(I00,i);
    Real &g11 = g(I11,i);
    Real &g12 = g(I12,i);
    Real &g22 = g(I22,i);
    Real &g33 = g(I33,i);
    Real &gi00 = g_inv(I00,i);
    Real &gi11 = g_inv(I11,i);
    Real &gi12 = g_inv(I12,i);
    Real &gi22 = g_inv(I22,i);
    Real &gi33 = g_inv(I33,i);

    // Set metric terms
    g00 = -1.0;
    g11 = alpha_sq;
    g12 = -beta;
    g22 = 1.0;
    g33 = 1.0;
    gi00 = -1.0;
    gi11 = 1.0;
    gi12 = beta;
    gi22 = alpha_sq;
    gi33 = 1.0;
  }
  return;
}

//--------------------------------------------------------------------------------------

// Function for computing face-centered metric coefficients: theta-interface
// Inputs:
//   k: phi-index
//   j: theta-index
// Outputs:
//   g: array of metric components in 1D
//   g_inv: array of inverse metric components in 1D
void Coordinates::Face2Metric(const int k, const int j, AthenaArray<Real> &g,
    AthenaArray<Real> &g_inv)
{
  #pragma simd
  for (int i = pmy_block->is; i <= pmy_block->ie; i++)
  {
    // Extract geometric quantities
    const Real &alpha_sq = metric_face2_i1_(i);
    const Real &beta = metric_face2_i2_(i);

    // Extract metric terms
    Real &g00 = g(I00,i);
    Real &g11 = g(I11,i);
    Real &g12 = g(I12,i);
    Real &g22 = g(I22,i);
    Real &g33 = g(I33,i);
    Real &gi00 = g_inv(I00,i);
    Real &gi11 = g_inv(I11,i);
    Real &gi12 = g_inv(I12,i);
    Real &gi22 = g_inv(I22,i);
    Real &gi33 = g_inv(I33,i);

    // Set metric terms
    g00 = -1.0;
    g11 = alpha_sq;
    g12 = -beta;
    g22 = 1.0;
    g33 = 1.0;
    gi00 = -1.0;
    gi11 = 1.0;
    gi12 = beta;
    gi22 = alpha_sq;
    gi33 = 1.0;
  }
  return;
}

//--------------------------------------------------------------------------------------

// Function for computing face-centered metric coefficients: phi-interface
// Inputs:
//   k: phi-index
//   j: theta-index
// Outputs:
//   g: array of metric components in 1D
//   g_inv: array of inverse metric components in 1D
void Coordinates::Face3Metric(const int k, const int j, AthenaArray<Real> &g,
    AthenaArray<Real> &g_inv)
{
  #pragma simd
  for (int i = pmy_block->is; i <= pmy_block->ie; i++)
  {
    // Extract geometric quantities
    const Real &alpha_sq = metric_face3_i1_(i);
    const Real &beta = metric_face3_i2_(i);

    // Extract metric terms
    Real &g00 = g(I00,i);
    Real &g11 = g(I11,i);
    Real &g12 = g(I12,i);
    Real &g22 = g(I22,i);
    Real &g33 = g(I33,i);
    Real &gi00 = g_inv(I00,i);
    Real &gi11 = g_inv(I11,i);
    Real &gi12 = g_inv(I12,i);
    Real &gi22 = g_inv(I22,i);
    Real &gi33 = g_inv(I33,i);

    // Set metric terms
    g00 = -1.0;
    g11 = alpha_sq;
    g12 = -beta;
    g22 = 1.0;
    g33 = 1.0;
    gi00 = -1.0;
    gi11 = 1.0;
    gi12 = beta;
    gi22 = alpha_sq;
    gi33 = 1.0;
  }
  return;
}

//--------------------------------------------------------------------------------------

// Function for transforming primitives to locally flat frame: x-interface
// Inputs:
//   k: z-index
//   j: y-index
//   b1_vals: 3D array of normal components B^1 of magnetic field, in global coordinates
//   prim_left: 1D array of left primitives, using global coordinates
//   prim_right: 1D array of right primitives, using global coordinates
// Outputs:
//   prim_left: values overwritten in local coordinates
//   prim_right: values overwritten in local coordinates
//   bx: 1D array of normal magnetic fields, in local coordinates
// Notes:
//   expects v1/v2/v3 in IVX/IVY/IVZ slots
//   expects B1 in b1_vals
//   expects B2/B3 in IBY/IBZ slots
//   puts vx/vy/vz in IVX/IVY/IVZ
//   puts Bx in bx
//   puts By/Bz in IBY/IBZ slots
void Coordinates::PrimToLocal1(const int k, const int j,
    const AthenaArray<Real> &b1_vals, AthenaArray<Real> &prim_left,
    AthenaArray<Real> &prim_right, AthenaArray<Real> &bx)
{
  // Go through 1D block of cells
  #pragma simd
  for (int i = pmy_block->is; i <= pmy_block->ie+1; i++)
  {
    // Extract geometric quantities
    const Real g00 = -1.0;
    const Real &g11 = metric_face1_i1_(i);
    const Real g12 = -metric_face1_i2_(i);
    const Real g22 = 1.0;
    const Real g33 = 1.0;
    const Real mt0 = 1.0;
    const Real mx1 = 1.0;
    const Real my1 = -trans_face1_i2_(i);
    const Real my2 = 1.0;
    const Real mz3 = 1.0;

    // Extract global 3-velocities
    Real &v1l = prim_left(IVX,i);
    Real &v2l = prim_left(IVY,i);
    Real &v3l = prim_left(IVZ,i);
    Real &v1r = prim_right(IVX,i);
    Real &v2r = prim_right(IVY,i);
    Real &v3r = prim_right(IVZ,i);

    // Construct global 4-velocities
    Real u0l = std::sqrt(-1.0 /
        (g00 + g11*v1l*v1l + 2.0*g12*v1l*v2l + g22*v2l*v2l + g33*v3l*v3l));
    Real u1l = u0l * v1l;
    Real u2l = u0l * v2l;
    Real u3l = u0l * v3l;
    Real u0r = std::sqrt(-1.0 /
        (g00 + g11*v1r*v1r + 2.0*g12*v1r*v2r + g22*v2r*v2r + g33*v3r*v3r));
    Real u1r = u0r * v1r;
    Real u2r = u0r * v2r;
    Real u3r = u0r * v3r;

    // Transform 4-velocities
    Real utl = mt0*u0l;
    Real uxl = mx1*u1l;
    Real uyl = my1*u1l + my2*u2l;
    Real uzl = mz3*u3l;
    Real utr = mt0*u0r;
    Real uxr = mx1*u1r;
    Real uyr = my1*u1r + my2*u2r;
    Real uzr = mz3*u3r;

    // Set local 3-velocities
    v1l = uxl / utl;
    v2l = uyl / utl;
    v3l = uzl / utl;
    v1r = uxr / utr;
    v2r = uyr / utr;
    v3r = uzr / utr;

    // Transform magnetic field if necessary
    if (MAGNETIC_FIELDS_ENABLED)
    {
      // Extract global magnetic fields
      const Real &b1 = b1_vals(k,j,i);
      Real &b2l = prim_left(IBY,i);
      Real &b3l = prim_left(IBZ,i);
      Real &b2r = prim_right(IBY,i);
      Real &b3r = prim_right(IBZ,i);

      // Construct global contravariant magnetic fields
      Real bcon0l = g11*b1*u1l + g12*(b1*u2l+b2l*u1l) + g22*b2l*u2l + g33*b3l*u3l;
      Real bcon1l = (b1 + bcon0l * u1l) / u0l;
      Real bcon2l = (b2l + bcon0l * u2l) / u0l;
      Real bcon3l = (b3l + bcon0l * u3l) / u0l;
      Real bcon0r = g11*b1*u1r + g12*(b1*u2r+b2r*u1r) + g22*b2r*u2r + g33*b3r*u3r;
      Real bcon1r = (b1 + bcon0r * u1r) / u0r;
      Real bcon2r = (b2r + bcon0r * u2r) / u0r;
      Real bcon3r = (b3r + bcon0r * u3r) / u0r;

      // Transform contravariant magnetic fields
      Real bcontl = mt0*bcon0l;
      Real bconxl = mx1*bcon1l;
      Real bconyl = my2*bcon2l;
      Real bconzl = mz3*bcon3l;
      Real bcontr = mt0*bcon0r;
      Real bconxr = mx1*bcon1r;
      Real bconyr = my2*bcon2r;
      Real bconzr = mz3*bcon3r;

      // Set local magnetic fields
      Real bxl = utl * bconxl - uxl * bcontl;
      Real bxr = utr * bconxr - uxr * bcontr;
      bx(i) = 0.5 * (bxl + bxr);
      b2l = utl * bconyl - uyl * bcontl;
      b3l = utl * bconzl - uzl * bcontl;
      b2r = utr * bconyr - uyr * bcontr;
      b3r = utr * bconzr - uzr * bcontr;
    }
  }
  return;
}

//--------------------------------------------------------------------------------------

// Function for transforming primitives to locally flat frame: y-interface
// Inputs:
//   k: z-index
//   j: y-index
//   b2_vals: 3D array of normal components B^2 of magnetic field, in global coordinates
//   prim_left: 1D array of left primitives, using global coordinates
//   prim_right: 1D array of right primitives, using global coordinates
// Outputs:
//   prim_left: values overwritten in local coordinates
//   prim_right: values overwritten in local coordinates
//   by: 1D array of normal magnetic fields, in local coordinates
// Notes:
//   expects v1/v2/v3 in IVX/IVY/IVZ slots
//   expects B2 in b2_vals
//   expects B3/B1 in IBY/IBZ slots
//   puts vx/vy/vz in IVY/IVZ/IVX slots
//   puts By in by
//   puts Bz/Bx in IBY/IBZ slots
void Coordinates::PrimToLocal2(const int k, const int j,
    const AthenaArray<Real> &b2_vals, AthenaArray<Real> &prim_left,
    AthenaArray<Real> &prim_right, AthenaArray<Real> &by)
{
  // Go through 1D block of cells
  #pragma simd
  for (int i = pmy_block->is; i <= pmy_block->ie; i++)
  {
    // Extract geometric quantities
    const Real g00 = -1.0;
    const Real &g11 = metric_face2_i1_(i);
    const Real g12 = -metric_face2_i2_(i);
    const Real g22 = 1.0;
    const Real g33 = 1.0;
    const Real mt0 = 1.0;
    const Real mx2 = 1.0 / trans_face2_i1_(i);
    const Real my3 = 1.0;
    const Real &mz1 = trans_face2_i1_(i);
    const Real mz2 = -trans_face2_i2_(i) / trans_face2_i1_(i);

    // Extract global 3-velocities
    Real &v1l = prim_left(IVX,i);
    Real &v2l = prim_left(IVY,i);
    Real &v3l = prim_left(IVZ,i);
    Real &v1r = prim_right(IVX,i);
    Real &v2r = prim_right(IVY,i);
    Real &v3r = prim_right(IVZ,i);

    // Construct global 4-velocities
    Real u0l = std::sqrt(-1.0 /
        (g00 + g11*v1l*v1l + 2.0*g12*v1l*v2l + g22*v2l*v2l + g33*v3l*v3l));
    Real u1l = u0l * v1l;
    Real u2l = u0l * v2l;
    Real u3l = u0l * v3l;
    Real u0r = std::sqrt(-1.0 /
        (g00 + g11*v1r*v1r + 2.0*g12*v1r*v2r + g22*v2r*v2r + g33*v3r*v3r));
    Real u1r = u0r * v1r;
    Real u2r = u0r * v2r;
    Real u3r = u0r * v3r;

    // Transform 4-velocities
    Real utl = mt0*u0l;
    Real uxl = mx2*u2l;
    Real uyl = my3*u3l;
    Real uzl = mz1*u1l + mz2*u2l;
    Real utr = mt0*u0r;
    Real uxr = mx2*u2r;
    Real uyr = my3*u3r;
    Real uzr = mz1*u1r + mz2*u2r;

    // Set local 3-velocities
    v2l = uxl / utl;
    v3l = uyl / utl;
    v1l = uzl / utl;
    v2r = uxr / utr;
    v3r = uyr / utr;
    v1r = uzr / utr;

    // Transform magnetic field if necessary
    if (MAGNETIC_FIELDS_ENABLED)
    {
      // Extract global magnetic fields
      const Real &b2 = b2_vals(k,j,i);
      Real &b3l = prim_left(IBY,i);
      Real &b1l = prim_left(IBZ,i);
      Real &b3r = prim_right(IBY,i);
      Real &b1r = prim_right(IBZ,i);

      // Construct global contravariant magnetic fields
      Real bcon0l = g11*b1l*u1l + g12*(b1l*u2l+b2*u1l) + g22*b2*u2l + g33*b3l*u3l;
      Real bcon1l = (b1l + bcon0l * u1l) / u0l;
      Real bcon2l = (b2 + bcon0l * u2l) / u0l;
      Real bcon3l = (b3l + bcon0l * u3l) / u0l;
      Real bcon0r = g11*b1r*u1r + g12*(b1r*u2r+b2*u1r) + g22*b2*u2r + g33*b3r*u3r;
      Real bcon1r = (b1r + bcon0r * u1r) / u0r;
      Real bcon2r = (b2 + bcon0r * u2r) / u0r;
      Real bcon3r = (b3r + bcon0r * u3r) / u0r;

      // Transform contravariant magnetic fields
      Real bcontl = mt0*bcon0l;
      Real bconxl = mx2*bcon2l;
      Real bconyl = my3*bcon3l;
      Real bconzl = mz1*bcon1l + mz2*bcon2l;
      Real bcontr = mt0*bcon0r;
      Real bconxr = mx2*bcon2r;
      Real bconyr = my3*bcon3r;
      Real bconzr = mz1*bcon1r + mz2*bcon2r;

      // Set local magnetic fields
      Real byl = utl * bconyl - uyl * bcontl;
      Real byr = utr * bconyr - uyr * bcontr;
      by(i) = 0.5 * (byl + byr);
      b3l = utl * bconzl - uzl * bcontl;
      b1l = utl * bconxl - uxl * bcontl;
      b3r = utr * bconzr - uzr * bcontr;
      b1r = utr * bconxr - uxr * bcontr;
    }
  }
  return;
}

//--------------------------------------------------------------------------------------

// Function for transforming primitives to locally flat frame: z-interface
// Inputs:
//   k: z-index
//   j: y-index
//   b3_vals: 3D array of normal components B^3 of magnetic field, in global coordinates
//   prim_left: 1D array of left primitives, using global coordinates
//   prim_right: 1D array of right primitives, using global coordinates
// Outputs:
//   prim_left: values overwritten in local coordinates
//   prim_right: values overwritten in local coordinates
//   bz: 1D array of normal magnetic fields, in local coordinates
// Notes:
//   expects v1/v2/v3 in IVX/IVY/IVZ slots
//   expects B3 in b3_vals
//   expects B1/B2 in IBY/IBZ slots
//   puts vx/vy/vz in IVZ/IVX/IVY slots
//   puts Bz in bz
//   puts Bx/By in IBY/IBZ slots
void Coordinates::PrimToLocal3(const int k, const int j,
    const AthenaArray<Real> &b3_vals, AthenaArray<Real> &prim_left,
    AthenaArray<Real> &prim_right, AthenaArray<Real> &bz)
{
  // Go through 1D block of cells
  #pragma simd
  for (int i = pmy_block->is; i <= pmy_block->ie; i++)
  {
    // Extract geometric quantities
    const Real g00 = -1.0;
    const Real &g11 = metric_face3_i1_(i);
    const Real g12 = -metric_face3_i2_(i);
    const Real g22 = 1.0;
    const Real g33 = 1.0;
    const Real mt0 = 1.0;
    const Real mx3 = 1.0;
    const Real my1 = 1.0;
    const Real mz1 = -trans_face3_i2_(i);
    const Real mz2 = 1.0;

    // Extract global 3-velocities
    Real &v1l = prim_left(IVX,i);
    Real &v2l = prim_left(IVY,i);
    Real &v3l = prim_left(IVZ,i);
    Real &v1r = prim_right(IVX,i);
    Real &v2r = prim_right(IVY,i);
    Real &v3r = prim_right(IVZ,i);

    // Construct global 4-velocities
    Real u0l = std::sqrt(-1.0 /
        (g00 + g11*v1l*v1l + 2.0*g12*v1l*v2l + g22*v2l*v2l + g33*v3l*v3l));
    Real u1l = u0l * v1l;
    Real u2l = u0l * v2l;
    Real u3l = u0l * v3l;
    Real u0r = std::sqrt(-1.0 /
        (g00 + g11*v1r*v1r + 2.0*g12*v1r*v2r + g22*v2r*v2r + g33*v3r*v3r));
    Real u1r = u0r * v1r;
    Real u2r = u0r * v2r;
    Real u3r = u0r * v3r;

    // Transform 4-velocities
    Real utl = mt0*u0l;
    Real uxl = mx3*u3l;
    Real uyl = my1*u1l;
    Real uzl = mz1*u1l + mz2*u2l;
    Real utr = mt0*u0r;
    Real uxr = mx3*u3r;
    Real uyr = my1*u1r;
    Real uzr = mz1*u1r + mz2*u2r;

    // Set local 3-velocities
    v3l = uxl / utl;
    v1l = uyl / utl;
    v2l = uzl / utl;
    v3r = uxr / utr;
    v1r = uyr / utr;
    v2r = uzr / utr;

    // Transform magnetic field if necessary
    if (MAGNETIC_FIELDS_ENABLED)
    {
      // Extract global magnetic fields
      const Real &b3 = b3_vals(k,j,i);
      Real &b1l = prim_left(IBY,i);
      Real &b2l = prim_left(IBZ,i);
      Real &b1r = prim_right(IBY,i);
      Real &b2r = prim_right(IBZ,i);

      // Construct global contravariant magnetic fields
      Real bcon0l = g11*b1l*u1l + g12*(b1l*u2l+b2l*u1l) + g22*b2l*u2l + g33*b3*u3l;
      Real bcon1l = (b1l + bcon0l * u1l) / u0l;
      Real bcon2l = (b2l + bcon0l * u2l) / u0l;
      Real bcon3l = (b3 + bcon0l * u3l) / u0l;
      Real bcon0r = g11*b1r*u1r + g12*(b1r*u2r+b2r*u1r) + g22*b2r*u2r + g33*b3*u3r;
      Real bcon1r = (b1r + bcon0r * u1r) / u0r;
      Real bcon2r = (b2r + bcon0r * u2r) / u0r;
      Real bcon3r = (b3 + bcon0r * u3r) / u0r;

      // Transform contravariant magnetic fields
      Real bcontl = mt0*bcon0l;
      Real bconxl = mx3*bcon3l;
      Real bconyl = my1*bcon1l;
      Real bconzl = mz1*bcon1l + mz2*bcon2l;
      Real bcontr = mt0*bcon0r;
      Real bconxr = mx3*bcon3r;
      Real bconyr = my1*bcon1r;
      Real bconzr = mz1*bcon1r + mz2*bcon2r;

      // Set local magnetic fields
      Real bzl = utl * bconzl - uzl * bcontl;
      Real bzr = utr * bconzr - uzr * bcontr;
      bz(i) = 0.5 * (bzl + bzr);
      b1l = utl * bconxl - uxl * bcontl;
      b2l = utl * bconyl - uyl * bcontl;
      b1r = utr * bconyr - uyr * bcontr;
      b2r = utr * bconzr - uzr * bcontr;
    }
  }
  return;
}

//--------------------------------------------------------------------------------------

// Function for transforming fluxes to global frame: x-interface
// Inputs:
//   k: z-index
//   j: y-index
//   flux: array of fluxes in 1D, using local coordinates
// Outputs:
//   flux: values overwritten in global coordinates
// Notes:
//   expects values and x-fluxes of Mx/My/Mz in IM1/IM2/IM3 slots
//   expects values and x-fluxes of By/Bz in IBY/IBZ slots
//   puts x1-fluxes of M1/M2/M3 in IM1/IM2/IM3 slots
//   puts x1-fluxes of B2/B3 in IBY/IBZ slots
void Coordinates::FluxToGlobal1(const int k, const int j, AthenaArray<Real> &flux)
{
  // Go through 1D block of cells
  #pragma simd
  for (int i = pmy_block->is; i <= pmy_block->ie+1; i++)
  {
    // Extract geometric quantities
    const Real g00 = -1.0;
    const Real &g11 = metric_face1_i1_(i);
    const Real g12 = -metric_face1_i2_(i);
    const Real g22 = 1.0;
    const Real g33 = 1.0;
    const Real m0t = 1.0;
    const Real m1x = 1.0;
    const Real &m2x = trans_face1_i2_(i);
    const Real m2y = 1.0;
    const Real m3z = 1.0;

    // Extract local conserved quantities and fluxes
    const Real dx = flux(IDN,i);
    const Real txt = flux(IEN,i);
    const Real txx = flux(IM1,i);
    const Real txy = flux(IM2,i);
    const Real txz = flux(IM3,i);

    // Transform stress-energy tensor
    const Real tcon_10 = m1x * m0t * txt;
    const Real tcon_11 = m1x * m1x * txx;
    const Real tcon_12 = m1x * (m2x * txx + m2y * txy);
    const Real tcon_13 = m1x * m3z * txz;

    // Extract global fluxes
    Real &d1 = flux(IDN,i);
    Real &t10 = flux(IEN,i);
    Real &t11 = flux(IM1,i);
    Real &t12 = flux(IM2,i);
    Real &t13 = flux(IM3,i);

    // Set fluxes
    d1 = m1x*dx;
    t10 = g00*tcon_10;
    t11 = g11*tcon_11 + g12*tcon_12;
    t12 = g12*tcon_11 + g22*tcon_12;
    t13 = g33*tcon_13;

    // Transform magnetic fluxes if necessary
    if (MAGNETIC_FIELDS_ENABLED)
    {
      const Real fyx = flux(IBY,i);
      const Real fzx = flux(IBZ,i);
      Real &f21 = flux(IBY,i);
      Real &f31 = flux(IBZ,i);
      f21 = m1x * m2y * fyx;
      f31 = m1x * m3z * fzx;
    }
  }
  return;
}

//--------------------------------------------------------------------------------------

// Function for transforming fluxes to global frame: y-interface
// Inputs:
//   k: z-index
//   j: y-index
//   flux: array of fluxes in 1D, using local coordinates
// Outputs:
//   flux: values overwritten in global coordinates
// Notes:
//   expects values and x-fluxes of Mx/My/Mz in IM2/IM3/IM1 slots
//   expects values and x-fluxes of By/Bz in IBY/IBZ slots
//   puts x2-fluxes of M1/M2/M3 in IM1/IM2/IM3 slots
//   puts x2-fluxes of B3/B1 in IBY/IBZ slots
void Coordinates::FluxToGlobal2(const int k, const int j, AthenaArray<Real> &flux)
{
  // Go through 1D block of cells
  #pragma simd
  for (int i = pmy_block->is; i <= pmy_block->ie; i++)
  {
    // Extract geometric quantities
    const Real g00 = -1.0;
    const Real &g11 = metric_face2_i1_(i);
    const Real g12 = -metric_face2_i2_(i);
    const Real g22 = 1.0;
    const Real g33 = 1.0;
    const Real m0t = 1.0;
    const Real m1x = trans_face2_i2_(i) / trans_face2_i1_(i);
    const Real m1z = 1.0 / trans_face2_i1_(i);
    const Real &m2x = trans_face2_i1_(i);
    const Real m3y = 1.0;

    // Extract local conserved quantities and fluxes
    const Real dx = flux(IDN,i);
    const Real txt = flux(IEN,i);
    const Real txx = flux(IM2,i);
    const Real txy = flux(IM3,i);
    const Real txz = flux(IM1,i);

    // Transform stress-energy tensor
    const Real tcon_20 = m2x * m0t * txt;
    const Real tcon_21 = m2x * (m1x * txx + m1z * txz);
    const Real tcon_22 = m2x * m2x * txx;
    const Real tcon_23 = m2x * m3y * txy;

    // Extract global fluxes
    Real &d2 = flux(IDN,i);
    Real &t20 = flux(IEN,i);
    Real &t21 = flux(IM1,i);
    Real &t22 = flux(IM2,i);
    Real &t23 = flux(IM3,i);

    // Set fluxes
    d2 = m2x*dx;
    t20 = g00*tcon_20;
    t21 = g11*tcon_21 + g12*tcon_22;
    t22 = g12*tcon_21 + g22*tcon_22;
    t23 = g33*tcon_23;

    // Transform magnetic fluxes if necessary
    if (MAGNETIC_FIELDS_ENABLED)
    {
      const Real fyx = flux(IBY,i);
      const Real fzx = flux(IBZ,i);
      Real &f32 = flux(IBY,i);
      Real &f12 = flux(IBZ,i);
      f32 = m3y * m2x * fyx;
      f12 = m2x * m1z * fzx;
    }
  }
  return;
}

//--------------------------------------------------------------------------------------

// Function for transforming fluxes to global frame: z-interface
// Inputs:
//   k: z-index
//   j: y-index
//   flux: array of fluxes in 1D, using local coordinates
// Outputs:
//   flux: values overwritten in global coordinates
// Notes:
//   expects values and x-fluxes of Mx/My/Mz in IM3/IM1/IM2 slots
//   expects values and x-fluxes of By/Bz in IBY/IBZ slots
//   puts x3-fluxes of M1/M2/M3 in IM1/IM2/IM3 slots
//   puts x3-fluxes of B1/B2 in IBY/IBZ slots
void Coordinates::FluxToGlobal3(const int k, const int j, AthenaArray<Real> &flux)
{
  // Go through 1D block of cells
  #pragma simd
  for (int i = pmy_block->is; i <= pmy_block->ie; i++)
  {
    // Extract geometric quantities
    const Real g00 = -1.0;
    const Real &g11 = metric_face3_i1_(i);
    const Real g12 = -metric_face3_i2_(i);
    const Real g22 = 1.0;
    const Real g33 = 1.0;
    const Real m0t = 1.0;
    const Real m1y = 1.0;
    const Real &m2y = trans_face3_i2_(i);
    const Real m2z = 1.0;
    const Real m3x = 1.0;

    // Extract local conserved quantities and fluxes
    const Real dx = flux(IDN,i);
    const Real txt = flux(IEN,i);
    const Real txx = flux(IM3,i);
    const Real txy = flux(IM1,i);
    const Real txz = flux(IM2,i);

    // Transform stress-energy tensor
    const Real tcon_30 = m3x * m0t * txt;
    const Real tcon_31 = m3x * m1y * txy;
    const Real tcon_32 = m3x * (m2y * txy + m2z * txz);
    const Real tcon_33 = m3x * m3x * txx;

    // Extract global fluxes
    Real &d3 = flux(IDN,i);
    Real &t30 = flux(IEN,i);
    Real &t31 = flux(IM1,i);
    Real &t32 = flux(IM2,i);
    Real &t33 = flux(IM3,i);

    // Set fluxes
    d3 = m3x*dx;
    t30 = g00*tcon_30;
    t31 = g11*tcon_31 + g12*tcon_32;
    t32 = g12*tcon_31 + g22*tcon_32;
    t33 = g33*tcon_33;

    // Transform magnetic fluxes if necessary
    if (MAGNETIC_FIELDS_ENABLED)
    {
      const Real fyx = flux(IBY,i);
      const Real fzx = flux(IBZ,i);
      Real &f13 = flux(IBY,i);
      Real &f23 = flux(IBZ,i);
      f13 = m1y * m3x * fyx;
      f23 = m3x * (m2y * fyx + m2z * fzx);
    }
  }
  return;
}

//--------------------------------------------------------------------------------------

// Function for calculating distance between two points
// Inputs:
//   a1,a2,a3: global coordinates of first point
//   bx,by,bz: Minkowski coordinates of second point
// Outputs:
//   returned value: Euclidean distance between a and b
Real Coordinates::DistanceBetweenPoints(Real a1, Real a2, Real a3, Real bx, Real by,
    Real bz)
{
  Real ax = a1;
  Real ay = a2 - A * std::sin(K * a1);
  Real az = a3;
  return std::sqrt(SQR(ax-bx) + SQR(ay-by) + SQR(az-bz));
}
