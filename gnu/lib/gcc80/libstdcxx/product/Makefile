RELUP=	/..
.include "../../Makefile.inc"
SRCDIR=	${GCCDIR}/libstdc++-v3
PREDIR=	${SRCDIR}/config/abi/pre
.include "Makefile.src"

LIB=		stdc++
SHLIB_MAJOR=	9

.PATH:	${SRCDIR}/src/c++98
.PATH:	${SRCDIR}/src/c++11

# for now do not let the use of -flto (-Wodr...)
.if ${CFLAGS:M-flto}
CFLAGS+=	-fno-lto
LDFLAGS+=	-flto
.endif

CFLAGS+=	-I${.OBJDIR}
CXXFLAGS+=	-fno-implicit-templates
CXXFLAGS+=	-fdiagnostics-show-location=once
CXXFLAGS+=	-ffunction-sections -fdata-sections
LDFLAGS+=	-Wl,-z -Wl,relro -Wl,--gc-sections
VERSION_MAP=	${.OBJDIR}/libstdcxx.map

FLAGS_GROUPS=		gnu11
gnu11_FLAGS=		-std=gnu++11
gnu11_FLAGS_FILES=	${cxx11_sources:S|++|xx|}

CONVARCS=	../components/libconv_supc/libsupcxxconvenience.a
CONVARCS+=	../components/libconv_1998/libcxx98convenience.a
CONVARCS+=	../components/libconv_2011/libcxx11convenience.a
LDADD=		-Wl,--whole-archive ${CONVARCS:.a=_pic.a} -lm
LDADD+=		-Wl,--no-whole-archive
ARADD=		${CONVARCS} ${LIBM}

# The AR script is a nice trick, but it *cannot* handle "++" on any line
# To work around this incredible limitation, copy files with "++" in
# their names to a new file name (transform ++ => xx)

SRCS=		${libstdc___la_SOURCES:S|++|xx|}
PROBLEMS=	${libstdc___la_SOURCES:M*++*}
PROBLEMSXX=	${PROBLEMS:S|++|xx|}

# Override bsd.lib.mk scripts
libstdc++.a: ${SRCS:.cc=.o} script.ar
	@${ECHO} building static ${LIB} library via script
	rm -f ${.TARGET}
	${AR} -M < script.ar
	mv libstdcxx.a ${.TARGET}
	${RANLIB} ${.TARGET}

libstdc++_p.a: ${SRCS:.cc=.po} script_p.ar
	@${ECHO} building profiled ${LIB} library via script
	rm -f ${.TARGET}
	${AR} -M < script_p.ar
	mv libstdcxx_p.a ${.TARGET}
	${RANLIB} ${.TARGET}

${PROBLEMSXX}: ${PROBLEMS}
	cp ${SRCDIR}/src/c++11/${.TARGET:S|xx|++|} ${.TARGET}

gstdint.h:
	@echo "#include <stdint.h>" > ${.TARGET}

script.ar:
	@echo "CREATE libstdcxx.a"	>  ${.TARGET}
.for arc in ${CONVARCS}
	@echo "ADDLIB ${arc}"		>> ${.TARGET}
.endfor
.for obj in ${SRCS:.cc=.o}
	@echo "ADDMOD ${obj}"		>> ${.TARGET}
.endfor
	@echo "SAVE"			>> ${.TARGET}
	@echo "END"			>> ${.TARGET}

script_p.ar:
	@echo "CREATE libstdcxx_p.a"	>  ${.TARGET}
.for arc in ${CONVARCS:.a=_p.a}
	@echo "ADDLIB ${arc}"		>> ${.TARGET}
.endfor
.for obj in ${SRCS:.cc=.po}
	@echo "ADDMOD ${obj}"		>> ${.TARGET}
.endfor
	@echo "SAVE"			>> ${.TARGET}
	@echo "END"			>> ${.TARGET}

libstdcxx.map: ${PREDIR}/gnu.ver ${PREDIR}/float128.ver
	cat ${.ALLSRC} | grep -E -v '^[	 ]*#(#| |$$)' | \
	${CC} -E -P -include ${.CURDIR}/../headers/config.h - > ${.TARGET}

GENFILES=	gstdint.h script.ar script_p.ar ${PROBLEMSXX} libstdcxx.map
CLEANFILES=	${GENFILES}
beforedepend:	${GENFILES}

.include <bsd.lib.mk>
