#
# Copyright (c) 2014 Martin Decky
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# - Redistributions of source code must retain the above copyright
#   notice, this list of conditions and the following disclaimer.
# - Redistributions in binary form must reproduce the above copyright
#   notice, this list of conditions and the following disclaimer in the
#   documentation and/or other materials provided with the distribution.
# - The name of the author may not be used to endorse or promote products
#   derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

deps = [ 'ui', 'gfximage', 'compress' ]

_images = files(
	'gfx/frame01.tga.gz',
	'gfx/frame02.tga.gz',
	'gfx/frame03.tga.gz',
	'gfx/frame04.tga.gz',
	'gfx/frame05.tga.gz',
	'gfx/frame06.tga.gz',
	'gfx/frame07.tga.gz',
	'gfx/frame08.tga.gz',
	'gfx/frame09.tga.gz',
	'gfx/frame10.tga.gz',
	'gfx/frame11.tga.gz',
	'gfx/frame12.tga.gz',
	'gfx/frame13.tga.gz',
	'gfx/frame14.tga.gz',
	'gfx/frame15.tga.gz',
	'gfx/frame16.tga.gz',
	'gfx/frame17.tga.gz',
	'gfx/frame18.tga.gz',
	'gfx/frame19.tga.gz',
	'gfx/frame20.tga.gz',
	'gfx/frame21.tga.gz',
	'gfx/frame22.tga.gz',
	'gfx/frame23.tga.gz',
	'gfx/frame24.tga.gz',
	'gfx/frame25.tga.gz',
	'gfx/frame26.tga.gz',
	'gfx/frame27.tga.gz',
	'gfx/frame28.tga.gz',
	'gfx/frame29.tga.gz',
	'gfx/frame30.tga.gz',
)

_tarball = custom_target('barber_images.tar',
	input: _images,
	output: 'barber_images.tar',
	command: tar,
)

# TODO

_images_zip = custom_target('barber_images.zip',
	input : _images,
	output : [ 'images.zip' ],
	command : [ mkarray, '@OUTDIR@', 'images', 'image', uspace_as_prolog, '.data', '@INPUT@' ],
)
_imgs_s = custom_target('barber_images.s',
	input : _images_zip,
	output : [ 'images.s' ],
	command : [ unzip, '-p', '@INPUT@', 'images.s' ],
	capture : true,
)
_imgs_h = custom_target('barber_images.h',
	input : _images_zip,
	output : [ 'images.h' ],
	command : [ unzip, '-p', '@INPUT@', 'images.h' ],
	capture : true,
)
_imgs_desc_c = custom_target('barber_images_desc.c',
	input : _images_zip,
	output : [ 'images_desc.c' ],
	command : [ unzip, '-p', '@INPUT@', 'images_desc.c' ],
	capture : true,
)

src = [ files('barber.c'), _imgs_s, _imgs_h, _imgs_desc_c ]
