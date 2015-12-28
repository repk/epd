SECTIONS
{
	.initcall : {
		PROVIDE(__initcall_start = .);
		*(.initcall)
		PROVIDE(__initcall_end = .);
	}
	.exitcall : {
		PROVIDE(__exitcall_start = .);
		*(.exitcall)
		PROVIDE(__exitcall_end = .);
	}
}

INSERT AFTER .text;

