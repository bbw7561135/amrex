
#include <AMReX_MLLinOp.H>
#include <AMReX_LO_F.H>

namespace amrex {

constexpr int MLLinOp::mg_coarsen_ratio;
constexpr int MLLinOp::mg_box_min_width;

MLLinOp::MLLinOp (const Vector<Geometry>& a_geom,
                  const Vector<BoxArray>& a_grids,
                  const Vector<DistributionMapping>& a_dmap)
{
    define(a_geom, a_grids, a_dmap);
}

void
MLLinOp::define (const Vector<Geometry>& a_geom,
                 const Vector<BoxArray>& a_grids,
                 const Vector<DistributionMapping>& a_dmap)
{
    m_num_amr_levels = a_geom.size();

    m_amr_ref_ratio.resize(m_num_amr_levels);
    m_num_mg_levels.resize(m_num_amr_levels);

    m_geom.resize(m_num_amr_levels);
    m_grids.resize(m_num_amr_levels);
    m_dmap.resize(m_num_amr_levels);

    m_undrrelxr.resize(m_num_amr_levels);

    m_maskvals.resize(m_num_amr_levels);

    m_fluxreg.resize(m_num_amr_levels-1);

    m_bndry_sol.resize(m_num_amr_levels);
    m_crse_sol_br.resize(m_num_amr_levels);

    m_bndry_cor.resize(m_num_amr_levels);
    m_crse_cor_br.resize(m_num_amr_levels);

    // fine amr levels
    for (int amrlev = m_num_amr_levels-1; amrlev > 0; --amrlev)
    {
        m_num_mg_levels[amrlev] = 1;
        m_geom[amrlev].push_back(a_geom[amrlev]);
        m_grids[amrlev].push_back(a_grids[amrlev]);
        m_dmap[amrlev].push_back(a_dmap[amrlev]);

        int rr = mg_coarsen_ratio;
        const Box& dom = a_geom[amrlev].Domain();
        for (int i = 0; i < 30; ++i)
        {
            if (!dom.coarsenable(rr)) amrex::Abort("MLLinOp: Uncoarsenable domain");

            const Box& cdom = amrex::coarsen(dom,rr);
            if (cdom == a_geom[amrlev-1].Domain()) break;

            ++(m_num_mg_levels[amrlev]);

            m_geom[amrlev].emplace_back(cdom);

            m_grids[amrlev].push_back(a_grids[amrlev]);
            AMREX_ASSERT(m_grids[amrlev].back().coarsenable(rr));
            m_grids[amrlev].back().coarsen(rr);

            m_dmap[amrlev].push_back(a_dmap[amrlev]);

            rr *= mg_coarsen_ratio;
        }

        m_amr_ref_ratio[amrlev-1] = rr;
    }

    // coarsest amr level
    m_num_mg_levels[0] = 1;
    m_geom[0].push_back(a_geom[0]);
    m_grids[0].push_back(a_grids[0]);
    m_dmap[0].push_back(a_dmap[0]);

    int rr = mg_coarsen_ratio;
    while (a_geom[0].Domain().coarsenable(rr)
           and a_grids[0].coarsenable(rr, mg_box_min_width))
    {
        ++(m_num_mg_levels[0]);

        m_geom[0].emplace_back(amrex::coarsen(a_geom[0].Domain(),rr));

        m_grids[0].push_back(a_grids[0]);
        m_grids[0].back().coarsen(rr);

        m_dmap[0].push_back(a_dmap[0]);
        
        rr *= mg_coarsen_ratio;
    }

    for (int amrlev = 0; amrlev < m_num_amr_levels; ++amrlev)
    {
        m_undrrelxr[amrlev].resize(m_num_mg_levels[amrlev]);
        for (int mglev = 0; mglev < m_num_mg_levels[amrlev]; ++mglev)
        {
            m_undrrelxr[amrlev][mglev].define(m_grids[amrlev][mglev],
                                              m_dmap[amrlev][mglev],
                                              1, 0, 0, 1);
        }
    }
    
    for (int amrlev = 0; amrlev < m_num_amr_levels; ++amrlev)
    {
        m_maskvals[amrlev].resize(m_num_mg_levels[amrlev]);
        for (int mglev = 0; mglev < m_num_mg_levels[amrlev]; ++mglev)
        {
            for (OrientationIter oitr; oitr; ++oitr)
            {
                const Orientation face = oitr();
                const int ngrow = 1;
                m_maskvals[amrlev][mglev][face].define(m_grids[amrlev][mglev],
                                                       m_dmap[amrlev][mglev],
                                                       m_geom[amrlev][mglev],
                                                       face, 0, ngrow, 0, 1, true);
            }
        }
    }

    for (int amrlev = 0; amrlev < m_num_amr_levels-1; ++amrlev)
    {
        const IntVect ratio{m_amr_ref_ratio[amrlev]};
        m_fluxreg[amrlev].define(m_grids[amrlev+1][0], m_grids[amrlev][0],
                                 m_dmap[amrlev+1][0], m_dmap[amrlev][0],
                                 m_geom[amrlev+1][0], m_geom[amrlev][0],
                                 ratio, amrlev+1, 1);
    }
    
    for (int amrlev = 0; amrlev < m_num_amr_levels; ++amrlev)
    {
        m_bndry_sol[amrlev].reset(new MacBndry(m_grids[amrlev][0], m_dmap[amrlev][0],
                                              1, m_geom[amrlev][0]));
    }

    for (int amrlev = 1; amrlev < m_num_amr_levels; ++amrlev)
    {
        const int ncomp = 1;
        const int in_rad = 0;
        const int out_rad = 1;
        const int extent_rad = 2;
        const int crse_ratio = m_amr_ref_ratio[amrlev-1];
        BoxArray cba = m_grids[amrlev][0];
        cba.coarsen(crse_ratio);
        m_crse_sol_br[amrlev].reset(new BndryRegister(cba, m_dmap[amrlev][0],
                                                      in_rad, out_rad, extent_rad, ncomp));
    }

    for (int amrlev = 1; amrlev < m_num_amr_levels; ++amrlev)
    {
        const int ncomp = 1;
        const int in_rad = 0;
        const int out_rad = 1;
        const int extent_rad = 2;
        const int crse_ratio = m_amr_ref_ratio[amrlev-1];
        BoxArray cba = m_grids[amrlev][0];
        cba.coarsen(crse_ratio);
        m_crse_cor_br[amrlev].reset(new BndryRegister(cba, m_dmap[amrlev][0],
                                                      in_rad, out_rad, extent_rad, ncomp));
        m_crse_cor_br[amrlev]->setVal(0.0);
    }

    // This has be to done after m_crse_cor_br is defined.
    for (int amrlev = 1; amrlev < m_num_amr_levels; ++amrlev)
    {
        m_bndry_cor[amrlev].reset(new MacBndry(m_grids[amrlev][0], m_dmap[amrlev][0],
                                              1, m_geom[amrlev][0]));
        // this will make it Dirichlet
        BCRec phys_bc{AMREX_D_DECL(Outflow,Outflow,Outflow),
                      AMREX_D_DECL(Outflow,Outflow,Outflow)};
        
        MultiFab bc_data(m_grids[amrlev][0], m_dmap[amrlev][0], 1, 1);
        bc_data.setVal(0.0);
        m_bndry_cor[amrlev]->setBndryValues(*m_crse_cor_br[amrlev], 0, bc_data, 0, 0, 1,
                                            m_amr_ref_ratio[amrlev-1], phys_bc);
    }
}

MLLinOp::~MLLinOp ()
{}

void
MLLinOp::make (Vector<Vector<MultiFab> >& mf, int nc, int ng) const
{
    mf.clear();
    mf.resize(m_num_amr_levels);
    for (int alev = 0; alev < m_num_amr_levels; ++alev)
    {
        mf[alev].resize(m_num_mg_levels[alev]);
        for (int mlev = 0; mlev < m_num_mg_levels[alev]; ++mlev)
        {
            mf[alev][mlev].define(m_grids[alev][mlev], m_dmap[alev][mlev], nc, ng);
        }
    }
}

void
MLLinOp::setDirichletBC (int amrlev, const MultiFab& bc_data, const MultiFab* crse_bcdata)
{
    // this will make it Dirichlet
    BCRec phys_bc{AMREX_D_DECL(Outflow,Outflow,Outflow),
                  AMREX_D_DECL(Outflow,Outflow,Outflow)};

    if (crse_bcdata == nullptr)
    {
        AMREX_ALWAYS_ASSERT(amrlev == 0);
        m_bndry_sol[amrlev]->setBndryValues(bc_data,0,0,1,phys_bc);
    }
    else
    {
        m_crse_sol_br[amrlev]->copyFrom(*crse_bcdata, 0, 0, 0, 1);
        m_bndry_sol[amrlev]->setBndryValues(*m_crse_sol_br[amrlev], 0, bc_data, 0, 0, 1,
                                            m_amr_ref_ratio[amrlev-1], phys_bc);
    }
}

void
MLLinOp::updateSolBC (int amrlev, const MultiFab& crse_bcdata)
{
    AMREX_ALWAYS_ASSERT(amrlev > 0);
    m_crse_sol_br[amrlev]->copyFrom(crse_bcdata, 0, 0, 0, 1);
    m_bndry_sol[amrlev]->updateBndryValues(*m_crse_sol_br[amrlev], 0, 0, 1, m_amr_ref_ratio[amrlev-1]);
}

void
MLLinOp::updateCorBC (int amrlev, const MultiFab& crse_bcdata)
{
    AMREX_ALWAYS_ASSERT(amrlev > 0);
    m_crse_cor_br[amrlev]->copyFrom(crse_bcdata, 0, 0, 0, 1);
    m_bndry_cor[amrlev]->updateBndryValues(*m_crse_cor_br[amrlev], 0, 0, 1, m_amr_ref_ratio[amrlev-1]);
}

void
MLLinOp::residual (int amrlev, int mglev,
                   MultiFab& resid, MultiFab& sol, const MultiFab& rhs,
                   BCMode bc_mode) const
{
    apply(amrlev, mglev, resid, sol, bc_mode);
    MultiFab::Xpay(resid, -1.0, rhs, 0, 0, resid.nComp(), 0);
}

void
MLLinOp::correctionResidual (int amrlev, MultiFab& resid, MultiFab& sol, const MultiFab& rhs) const
{
    AMREX_ALWAYS_ASSERT(amrlev > 0);
    const int mglev = 0;
    apply(amrlev, mglev, resid, sol, BCMode::Inhomogeneous, m_bndry_cor[amrlev].get());
    MultiFab::Xpay(resid, -1.0, rhs, 0, 0, resid.nComp(), 0);
}

void
MLLinOp::apply (int amrlev, int mglev, MultiFab& out, MultiFab& in, BCMode bc_mode,
                const MacBndry* bndry) const
{
    applyBC(amrlev, mglev, in, bc_mode, bndry);
    Fapply(amrlev, mglev, out, in);
}

void
MLLinOp::applyBC (int amrlev, int mglev, MultiFab& in, BCMode bc_mode, const MacBndry* bndry) const
{
    // No coarsened boundary values, cannot apply inhomog at mglev>0.
    BL_ASSERT(mglev == 0 || bc_mode == BCMode::Homogeneous);

    const bool cross = true;
    in.FillBoundary(0, 1, m_geom[amrlev][mglev].periodicity(), cross);

    int flagbc = (bc_mode == BCMode::Homogeneous) ? 0 : 1;

    int flagden = 1;  // Fill in undrrelxr

    const int num_comp = 1;
    const Real* h = m_geom[amrlev][mglev].CellSize();

    const MacBndry& macbndry = (bndry == nullptr) ? *m_bndry_sol[amrlev] : *bndry;
    BndryRegister& undrrelxr = m_undrrelxr[amrlev][mglev];
    const auto& maskvals = m_maskvals[amrlev][mglev];

#ifdef _OPENMP
#pragma omp parallel
#endif
    for (MFIter mfi(in, MFItInfo().SetDynamic(true));
         mfi.isValid(); ++mfi)
    {
        const BndryData::RealTuple      & bdl = macbndry.bndryLocs(mfi);
        const Vector<Vector<BoundCond> >& bdc = macbndry.bndryConds(mfi);

        for (OrientationIter oitr; oitr; ++oitr)
        {
            const Orientation ori = oitr();

            int           cdr = ori;
            Real          bcl = bdl[ori];
            int           bct = bdc[ori][0];

            const Box&       vbx   = mfi.validbox();
            FArrayBox&       iofab = in[mfi];

            FArrayBox&       ffab  = undrrelxr[ori][mfi];
            const FArrayBox& fsfab = macbndry.bndryValues(ori)[mfi];

            const Mask&   m   = maskvals[ori][mfi];

            FORT_APPLYBC(&flagden, &flagbc, &maxorder,
                         iofab.dataPtr(),
                         ARLIM(iofab.loVect()), ARLIM(iofab.hiVect()),
                         &cdr, &bct, &bcl,
                         fsfab.dataPtr(), 
                         ARLIM(fsfab.loVect()), ARLIM(fsfab.hiVect()),
                         m.dataPtr(),
                         ARLIM(m.loVect()), ARLIM(m.hiVect()),
                         ffab.dataPtr(),
                         ARLIM(ffab.loVect()), ARLIM(ffab.hiVect()),
                         vbx.loVect(),
                         vbx.hiVect(), &num_comp, h);
        }
    }

}

void
MLLinOp::smooth (int amrlev, int mglev, MultiFab& sol, const MultiFab& rhs, BCMode bc_mode) const
{
    for (int redblack = 0; redblack < 2; ++redblack)
    {
        applyBC(amrlev, mglev, sol, bc_mode);
        Fsmooth(amrlev, mglev, sol, rhs, redblack);
    }
}

void
MLLinOp::reflux (int crse_amrlev, MultiFab& res,
                 const MultiFab& crse_sol, const MultiFab& fine_sol) const
{
    YAFluxRegister& fluxreg = m_fluxreg[crse_amrlev];
    fluxreg.reset();

    Real dt = 1.0;
    const Real* crse_dx = m_geom[crse_amrlev  ][0].CellSize();
    const Real* fine_dx = m_geom[crse_amrlev+1][0].CellSize();

#ifdef _OPENMP
#pragma omp parallel
#endif
    {
        std::array<FArrayBox,AMREX_SPACEDIM> flux;
        std::array<FArrayBox const*,AMREX_SPACEDIM> pflux { AMREX_D_DECL(&flux[0], &flux[1], &flux[2]) };

        for (MFIter mfi(crse_sol, MFItInfo().SetDynamic(true));  mfi.isValid(); ++mfi)
        {
            if (fluxreg.CrseHasWork(mfi))
            {
                const Box& tbx = mfi.tilebox();
                AMREX_D_TERM(flux[0].resize(amrex::surroundingNodes(tbx,0));,
                             flux[1].resize(amrex::surroundingNodes(tbx,1));,
                             flux[2].resize(amrex::surroundingNodes(tbx,2)););
                FFlux(crse_amrlev, mfi, flux, crse_sol[mfi]);
                fluxreg.CrseAdd(mfi, pflux, crse_dx, dt);
            }
        }

        for (MFIter mfi(fine_sol, MFItInfo().SetDynamic(true));  mfi.isValid(); ++mfi)
        {
            if (fluxreg.FineHasWork(mfi))
            {
                const Box& tbx = mfi.tilebox();
                AMREX_D_TERM(flux[0].resize(amrex::surroundingNodes(tbx,0));,
                             flux[1].resize(amrex::surroundingNodes(tbx,1));,
                             flux[2].resize(amrex::surroundingNodes(tbx,2)););
                const int face_only = true;
                FFlux(crse_amrlev+1, mfi, flux, fine_sol[mfi], face_only);
                fluxreg.FineAdd(mfi, pflux, fine_dx, dt);            
            }
        }
    }

    fluxreg.Reflux(res);
}

}
