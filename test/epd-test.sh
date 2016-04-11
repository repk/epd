#!/bin/sh

usage() {
	echo -e "Usage: $(basename $0) [OPTIONS]" >&2
	echo -e "\t-c: Clean the screen" >&2
	echo -e "\t-b: Write black frame" >&2
	echo -e "\t-w <image.xbm>: Write an xbm image on the screen" >&2
	exit -1
}

get_args() {
	while [ ${#} -ge 1 ]; do
		if [ "${1}" = "-c" ]; then
			if [ ${#} -ne 1 ]; then
				usage
			fi
			ACTION="CLEAN"
		elif [ "${1}" = "-b" ]; then
			if [ ${#} -ne 1 ]; then
				usage
			fi
			ACTION="BLACK"
		elif [ "${1}" = "-w" ]; then
			if [ ${#} -ne 2 ]; then
				usage
			fi
			ACTION="WRITE"
			IMAGE="${2}"
			shift 1
		else
			usage
		fi
		shift 1
	done

	if [ -z "${ACTION}" ]; then
		usage
	fi
}

main() {
	get_args "${@}"

	if [ -n "${IMAGE}" ]; then
		tail -n +4 ${IMAGE} | xxd -r -p >> ${EPDFBDEV}
	fi

	case "${ACTION}" in
	"CLEAN")
		echo -n "C0" > ${EPDCTLDEV}
		;;
	"BLACK")
		echo -n "B0" > ${EPDCTLDEV}
		;;
	"WRITE")
		echo -n "W0" > ${EPDCTLDEV}
		;;
	esac
}

EPDFBDEV="/dev/epd0"
EPDCTLDEV="/dev/epdctl"
main "${@}"
