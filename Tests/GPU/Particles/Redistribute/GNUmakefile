AMREX_HOME ?= ../../../../

DEBUG	= TRUE
DEBUG	= FALSE

DIM	= 3

COMP    = pgi
NVCC_HOST_COMP = gcc

TINY_PROFILE = TRUE
USE_PARTICLES = TRUE

PRECISION = DOUBLE

USE_MPI     = FALSE
USE_OMP     = FALSE
USE_CUDA    = TRUE
USE_ACC     = TRUE

###################################################

EBASE     = main

include $(AMREX_HOME)/Tools/GNUMake/Make.defs

include ./Make.package
include $(AMREX_HOME)/Src/Base/Make.package
include $(AMREX_HOME)/Src/Particle/Make.package
include $(AMREX_HOME)/Tools/GNUMake/Make.rules
