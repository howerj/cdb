#!/usr/bin/env python

# Python implementation of cdb

# calc hash value with a given key


def calc_hash(s):
  return reduce(lambda h,c: (((h << 5) + h) ^ ord(c)) & 0xffffffffL, s, 5381)

# cdbget(fp, basepos, key)
def cdbget(fp, pos_header, k):
  from struct import unpack

  r = []
  h = calc_hash(k)
  
  fp.seek(pos_header + (h % 256)*(4+4))
  (pos_bucket, ncells) = unpack('<LL', fp.read(4+4))
  if ncells == 0: raise KeyError

  start = (h >> 8) % ncells
  for i in range(ncells):
    fp.seek(pos_bucket + ((start+i) % ncells)*(4+4))
    (h1, p1) = unpack('<LL', fp.read(4+4))
    if p1 == 0: raise KeyError
    if h1 == h:
      fp.seek(p1)
      (klen, vlen) = unpack('<LL', fp.read(4+4))
      k1 = fp.read(klen)
      v1 = fp.read(vlen)
      if k1 == k:
        r.append(v1)
        break
  else:
    raise KeyError
      
  return r


# cdbmake(filename, hash)
def cdbmake(f, a):
  from struct import pack

  # write cdb
  def write_cdb(fp):
    pos_header = fp.tell()

    # skip header
    p = pos_header+(4+4)*256  # sizeof((h,p))*256
    fp.seek(p)
    
    bucket = [ [] for i in range(256) ]
    # write data & make hash
    for (k,v) in a.iteritems():
      fp.write(pack('<LL',len(k), len(v)))
      fp.write(k)
      fp.write(v)
      h = calc_hash(k)
      bucket[h % 256].append((h,p))
      # sizeof(keylen)+sizeof(datalen)+sizeof(key)+sizeof(data)
      p += 4+4+len(k)+len(v)

    pos_hash = p
    # write hashes
    for b1 in bucket:
      if b1:
        ncells = len(b1)*2
        cell = [ (0,0) for i in range(ncells) ]
        for (h,p) in b1:
          i = (h >> 8) % ncells
          while cell[i][1]:  # is call[i] already occupied?
            i = (i+1) % ncells
          cell[i] = (h,p)
        for (h,p) in cell:
          fp.write(pack('<LL', h, p))
    
    # write header
    fp.seek(pos_header)
    for b1 in bucket:
      fp.write(pack('<LL', pos_hash, len(b1)*2))
      pos_hash += (len(b1)*2)*(4+4)
    return

  # main
  fp=file(f, "wb")
  write_cdb(fp)
  fp.close()
  return


# cdbmake by python-cdb
def cdbmake_true(f, a):
  import cdb
  c = cdb.cdbmake(f, f+".tmp")
  for (k,v) in a.iteritems():
    c.add(k,v)
  c.finish()
  return


# test suite
def test(n):
  import os
  from random import randint
  a = {}
  def randstr():
    return "".join([ chr(randint(32,126)) for i in xrange(randint(1,1000)) ])
  for i in xrange(n):
    a[randstr()] = randstr()
  #a = {"a":"1", "bcd":"234", "def":"567"}
  #a = {"a":"1"}
  cdbmake("my.cdb", a)
  cdbmake_true("true.cdb", a)
  # check the correctness
  os.system("cmp my.cdb true.cdb")

  fp = file("my.cdb")
  # check if all values are correctly obtained
  for (k,v) in a.iteritems():
    (v1,) = cdbget(fp, 0, k)
    assert v1 == v, "diff: "+repr(k)
  # check if nonexistent keys get error
  for i in xrange(n*2):
    k = randstr()
    try:
      v = a[k]
    except KeyError:
      try:
        cdbget(fp, 0, k)
        assert 0, "found: "+k
      except KeyError:
        pass
  fp.close()
  return

if __name__ == "__main__":
  test(1000)
