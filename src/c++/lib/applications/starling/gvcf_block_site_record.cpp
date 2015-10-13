// -*- mode: c++; indent-tabs-mode: nil; -*-
//
// Starka
// Copyright (c) 2009-2014 Illumina, Inc.
//
// This software is provided under the terms and conditions of the
// Illumina Open Source Software License 1.
//
// You should have received a copy of the Illumina Open Source
// Software License 1 along with this program. If not, see
// <https://github.com/sequencing/licenses/>
//

///
/// \author Chris Saunders
///

#include "gvcf_block_site_record.hh"
#include "blt_util/compat_util.hh"



static
bool
check_block_single_tolerance(const stream_stat& ss,
                             const int min,
                             const int tol)
{
    return ((min + tol) >= ss.max()/2.0);       // hack to get nova vcfs into acceptable size ramge, make check less stringent across the board
}



static
bool
check_block_tolerance(const stream_stat& ss,
                      const double frac_tol,
                      const int abs_tol)
{
    const int min(static_cast<int>(compat_round(ss.min())));
//    log_os << min << "\n";
//    return true;
    if (check_block_single_tolerance(ss,min,abs_tol)) return true;
    const int ftol(static_cast<int>(std::floor(min * frac_tol)));
    if (ftol <= abs_tol) return false;
    return check_block_single_tolerance(ss, min, ftol);
}



static
bool
is_new_value_blockable(const int new_val,
                       const stream_stat& ss,
                       const double frac_tol,
                       const int abs_tol,
                       const bool is_new_val = true,
                       const bool is_old_val = true)
{
    if (!(is_new_val && is_old_val)) return (is_new_val == is_old_val);

    stream_stat ss2(ss);
    ss2.add(new_val);
    return check_block_tolerance(ss2,frac_tol,abs_tol);
}



bool
gvcf_block_site_record::
test(const digt_site_info& si) const
{
    if (count==0) return true;

    // pos must be +1 from end of record:
    if ((pos+count) != si.pos) return false;

    // filters must match:
    if (filters != si.smod.filters) return false;

    if (is_nonref || si.is_nonref()) return false;

    if (gt != si.get_gt()) return false;

    // coverage states must match:
    if (is_covered != si.smod.is_covered) return false;
    if (is_used_covered != si.smod.is_used_covered) return false;

    // ploidy must match
    if (ploidy != si.dgt.ploidy) return false;

    // test blocking values:
    if (! is_new_value_blockable(si.smod.gqx,
                                 block_gqx,frac_tol,abs_tol,
                                 si.smod.is_gqx(),
                                 has_call))
    {
        return false;
    }

    if (! is_new_value_blockable(si.n_used_calls,
                                 block_dpu,frac_tol,abs_tol))
    {
        return false;
    }
    if (! is_new_value_blockable(si.n_unused_calls,
                                 block_dpf,frac_tol,abs_tol))
    {
        return false;
    }

    return true;
}



void
gvcf_block_site_record::
join(const digt_site_info& si)
{
    if (count == 0)
    {
        pos = si.pos;
        filters = si.smod.filters;
        is_nonref = si.is_nonref();
        gt = si.get_gt();
        // sites that are 0/1 can be compressed if their non-ref allele ratios are low enough. So, fix those here
        if ("0/1" == gt)
        {
            gt = "0/0";
        }
        is_used_covered = si.smod.is_used_covered;
        is_covered = si.smod.is_covered;
        ploidy = si.dgt.ploidy;
        has_call = si.smod.is_gqx();
        ref = si.ref;
    }

    if (si.smod.is_gqx())
    {
        block_gqx.add(si.smod.gqx);
    }

    block_dpu.add(si.n_used_calls);
    block_dpf.add(si.n_unused_calls);

    count += 1;
}

bool
gvcf_block_site_record::
test(const continuous_site_info& si) const
{
    if (si.calls.size() != 1)
        return false;

    if (count==0) return true;

    if (has_call && si.calls.empty())
        return false;


    // ploidy must match. This catches mixing digt && continuous
    if (ploidy != -1) return false;

    // pos must be +1 from end of record:
    if ((pos+count) != si.pos) return false;

    // filters must match:
    if (filters != si.calls.front().filters) return false;

    if (is_nonref || si.is_nonref()) return false;

    if (gt != si.get_gt(si.calls.front())) return false;

    // coverage states must match:
    if (is_covered != (si.n_used_calls != 0 || si.n_unused_calls != 0)) return false;
    if (is_used_covered != (si.n_used_calls != 0)) return false;

    if (has_call)
    {
        // test blocking values:
        if (! is_new_value_blockable(si.calls.front().gqx,
                block_gqx,frac_tol,abs_tol,
                true,
                true))
        {
            return false;
        }
    }

    if (! is_new_value_blockable(si.n_used_calls,
                                 block_dpu,frac_tol,abs_tol))
    {
        return false;
    }
    if (! is_new_value_blockable(si.n_unused_calls,
                                 block_dpf,frac_tol,abs_tol))
    {
        return false;
    }

    return true;
}



void
gvcf_block_site_record::join(const continuous_site_info& si)
{
    if (count == 0)
    {
        pos = si.pos;
        if (!si.calls.empty())
        {
            filters = si.calls.front().filters;
            gt = si.get_gt(si.calls.front());
            // sites that are 0/1 can be compressed if their non-ref allele ratios are low enough. So, fix those here
            if ("0/1" == gt)
            {
                gt = "0/0";
            }

            is_nonref = si.is_nonref();
            // TODO: handle no coverage regions in continuous
            has_call = true;
        }
        else
        {
            has_call = false;
        }
        is_used_covered = si.n_used_calls != 0;
        is_covered = si.n_used_calls != 0 || si.n_unused_calls != 0;
        ploidy = -1;
        ref = si.ref;
    }

    // TODO: handle no coverage regions in continuous
    if (si.calls.size() == 1)
    {
        block_gqx.add(si.calls.front().gqx);
    }

    block_dpu.add(si.n_used_calls);
    block_dpf.add(si.n_unused_calls);

    count += 1;
}



