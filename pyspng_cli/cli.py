import os
import os.path
import sys

import click
import pyspng
import numpy as np

@click.command()
@click.option("-c/-d", "--compress/--decompress", default=True, is_flag=True, help="Compress from or decompress to a numpy .npy file.", show_default=True)
@click.option("--header", default=False, is_flag=True, help="Print the header for the file.", show_default=True)
@click.option("-l", "--level", default=6, help="Set the compression level.", show_default=True)
@click.option("-p", "--progressive", default=False, is_flag=True, help="Create a progressive PNG (can load top to bottom on a slow connection). Mutually exlusive with --interlaced.", show_default=True)
@click.option("-i", "--interlaced", default=False, is_flag=True, help="Create an Adam7 interlaced progressive PNG (loads in increasing resolution on a slow connection). Mutually exlusive with --progressive.", show_default=True)
@click.argument("source", nargs=-1)
def main(compress, header, source, level, progressive, interlaced):
	"""
	Compress and decompress png files to and from numpy .npy files.

	This CLI program is BSD licensed.
	Author: William Silversmith
	"""
	if level < 0 or level > 9:
		print(f"pyspng: compress level {level} not valid. Must be 0-9 inclusive.")
		return
	if progressive and interlaced:
		print(f"pyspng: only progressive or interlaced can be specified at once.")
		return

	for i in range(len(source)):
		if source[i] == "-":
			source = source[:i] + sys.stdin.readlines() + source[i+1:]
	
	for src in source:
		if header:
			print_header(src)
			continue

		if compress:
			compress_file(src, level, progressive, interlaced)
		else:
			decompress_file(src)

def print_header(src):
	try:
		with open(src, "rb") as f:
			binary = f.read()
	except FileNotFoundError:
		print(f"pyspng: File \"{src}\" does not exist.")
		return

	head = pyspng.header(binary)
	print(f"Filename: {src}")
	for key,val in head.items():
		print(f"{key}: {val}")
	print()

def decompress_file(src):
	try:
		with open(src, "rb") as f:
			binary = f.read()
	except FileNotFoundError:
		print(f"pyspng: File \"{src}\" does not exist.")
		return

	try:
		data = pyspng.load(binary)
	except:
		print(f"pyspng: {src} could not be decoded.")
		return

	del binary

	dest = src.replace(".png", "")
	_, ext = os.path.splitext(dest)
	
	if ext != ".npy":
		dest += ".npy"

	np.save(dest, data)

	try:
		stat = os.stat(dest)
		if stat.st_size > 0:
			os.remove(src)
		else:
			raise ValueError("File is zero length.")
	except (FileNotFoundError, ValueError) as err:
		print(f"pyspng: Unable to write {dest}. Aborting.")
		sys.exit()

def compress_file(src, level, progressive, interlaced):
	try:
		data = np.load(src)
	except ValueError:
		print(f"pyspng: {src} is not a numpy file.")
		return
	except FileNotFoundError:
		print(f"pyspng: File \"{src}\" does not exist.")
		return

	if progressive:
		mode = pyspng.ProgressiveMode.PROGRESSIVE
	elif interlaced:
		mode = pyspng.ProgressiveMode.INTERLACED
	else:
		mode = pyspng.ProgressiveMode.NONE

	binary = pyspng.encode(data, progressive=mode, compress_level=level)
	del data

	dest = src.replace(".npy", "")
	dest = f"{dest}.png"
	with open(dest, "wb") as f:
		f.write(binary)
	del binary

	try:
		stat = os.stat(dest)
		if stat.st_size > 0:
			os.remove(src)
		else:
			raise ValueError("File is zero length.")
	except (FileNotFoundError, ValueError) as err:
		print(f"pyspng: Unable to write {dest}. Aborting.")
		sys.exit()

