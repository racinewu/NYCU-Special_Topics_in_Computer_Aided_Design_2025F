#pragma once

inline constexpr double PRIMARY_OUTPUT_LOADING = 0.03;  // pF
inline constexpr double WIRE_DELAY             = 0.005; // ns
inline constexpr double VDD                    = 0.9;   // V

inline constexpr int UNSET = -1;
inline constexpr int LOW   =  0;
inline constexpr int HIGH  =  1;


enum class Cell_Type { NOR2X1, INVX1, NANDX1 };

enum class Net_Type { Input, Output, Wire };

// Sort key for output ordering
enum class SortKey {
    InstanceNumber,
    PropagationDelay,
    OutputLoading,
    InternalPower,
    SwitchingPower
};