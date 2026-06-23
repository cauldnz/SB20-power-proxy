"""Parity guard: the on-device C++ calibration fit must match the Python oracle.

The firmware fits a meter-to-meter correction on-device (firmware/lib/proxy/CalibrationFit.h),
mirroring sb20proxy.calibration. This test pins the SHARED golden dataset + expected fit so the two
implementations can't silently drift: the SAME `duts`, the SAME ref model, and the SAME expected
breakpoints appear in firmware/test/test_proxy/test_main.cpp (`goldenCalPairs` /
`test_fit_curve_matches_python_grid`). If you change the fit on one side, this test (or the C++ one)
fails until both agree.
"""

from __future__ import annotations

from sb20proxy.calibration import fit_grid, fit_scale_offset, residual_watts

# Shared golden dataset — keep identical to goldenCalPairs() in test_main.cpp.
# XCadey-like DUT reads high; true ref = dut * (1.10 - 0.0003*dut).
_DUTS = [60, 70, 80, 110, 120, 130, 160, 170, 180,
         210, 220, 230, 260, 270, 280, 310, 320, 330]


def _golden_pairs() -> list[tuple]:
    return [(float(d), round(d * (1.10 - 0.0003 * d), 3), 90.0) for d in _DUTS]


def test_grid_breakpoints_match_the_cpp_golden():
    g = fit_grid(_golden_pairs(), target="xcadey", ref="assioma", n_bins=6)
    # These exact breakpoints are asserted in test_main.cpp::test_fit_curve_matches_python_grid.
    assert g.breakpoints == [
        [70.0, 1.079], [120.0, 1.064], [170.0, 1.049],
        [220.0, 1.034], [270.0, 1.019], [320.0, 1.004],
    ]


def test_scale_offset_matches_the_cpp_golden():
    so = fit_scale_offset(_golden_pairs(), target="xcadey", ref="assioma")
    # asserted in test_main.cpp::test_fit_scale_offset_matches_python (scale 0.983, offset 9.2).
    assert so.scale == 0.983
    assert so.offset == 9.2


def test_grid_residual_is_sub_watt_on_clean_data():
    g = fit_grid(_golden_pairs(), target="xcadey", ref="assioma", n_bins=6)
    res = residual_watts(g, _golden_pairs())
    # the C++ side asserts residualMeanW(...) < 1.0 W on the same data.
    assert res["mean_w"] < 1.0
