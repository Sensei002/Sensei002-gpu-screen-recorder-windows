wayland_scanner = dependency('wayland-scanner', native: true)
wayland_scanner_path = wayland_scanner.get_variable(pkgconfig: 'wayland_scanner')
wayland_scanner_prog = find_program(wayland_scanner_path, native: true)

wayland_scanner_code = generator(
	wayland_scanner_prog,
	output: '@BASENAME@-protocol.c',
	arguments: ['private-code', '@INPUT@', '@OUTPUT@'],
)

wayland_scanner_client = generator(
	wayland_scanner_prog,
	output: '@BASENAME@-client-protocol.h',
	arguments: ['client-header', '@INPUT@', '@OUTPUT@'],
)

protocols = [
	'xdg-output-unstable-v1.xml',
	'xdg-shell.xml',
	'wlr-layer-shell-unstable-v1.xml',
	'wlr-foreign-toplevel-management-unstable-v1.xml',
	'ext-data-control-v1.xml',
]

protocol_src = []
foreach xml : protocols
	protocol_src += wayland_scanner_code.process(xml)
	protocol_src += wayland_scanner_client.process(xml)
endforeach
