// Gecko/Firefox type resolvers for DBM
// These resolvers help pretty-print common Mozilla types
mdb.log(`Loading resolvers for libxul.so...`)

// nsTArray<T> - Dynamic array type
registerResolver(
  {
    name: 'nsTArray',
    match: 'nsTArray<.*?>',
    objectFileName: 'libxul.so',
  },
  function (variable, offset, count) {
    mdb.log('running resolver for nsTArray')
    const hdrPtr = variable.member('mHdr')
    const hdr = hdrPtr.deref(1)
    const length = hdr.member('mLength')
    if (!hdr || !length) {
      mdb.log('could not get hdr or length')
      return []
    }

    mdb.log(`length=${length} of type=${length.type()}`)

    const len = Number(length)

    if (len === 0) {
      return []
    }

    if (len > 10) {
      throw new Error(`We are not expecting length to be more than 1, really but as ${len}`)
    }

    const headerSize = hdr.type().sizeOf()

    mdb.log(`hdr addr=${hdr.address()}, size of hdr=${headerSize}`)
    const address = BigInt(hdr.address()) + BigInt(headerSize)

    const elementType = variable.type().templateArgument(0)
    mdb.log(`trying to return array of ${len} length of type ${elementType} at address ${address}`)
    return createArrayAt(address, elementType, len, variable)
  }
)

// nsAString / nsString - Mozilla's 16-bit string types
registerResolver(
  {
    name: 'nsAString',
    match: 'nsAString|nsString|nsAString_internal|nsString_internal',
    objectFileName: 'libxul.so',
  },
  function (variable, offset, count) {
    mdb.log('running resolver for nsAString/nsString')

    const dataPtr = variable.member('mData')
    const length = variable.member('mLength')

    if (!dataPtr || !length) {
      mdb.log('could not get mData or mLength')
      return []
    }

    const len = Number(length)
    mdb.log(`nsAString length=${len}`)

    if (len === 0) {
      return []
    }

    if (len > 1000) {
      mdb.log(`String too long (${len}), truncating to 1000 characters`)
      len = 1000
    }

    const elementType = variable.type().templateArgument(0)
    mdb.log(`trying to return array of ${len} length of type ${elementType} at address ${address}`)
    return createArrayAt(BigInt(dataPtr), elementType, len, variable)
  }
)

// nsACString / nsCString - Mozilla's 8-bit string types
registerResolver(
  {
    name: 'nsACString',
    match: 'nsACString|nsCString|nsACString_internal|nsCString_internal',
    objectFileName: 'libxul.so',
  },
  function (variable, offset, count) {
    mdb.log('running resolver for nsACString/nsCString')

    const dataPtr = variable.member('mData')
    const length = variable.member('mLength')

    if (!dataPtr || !length) {
      mdb.log('could not get mData or mLength')
      return []
    }

    const len = Number(length)
    mdb.log(`nsACString length=${len}`)

    if (len === 0) {
      return []
    }

    if (len > 1000) {
      mdb.log(`String too long (${len}), truncating to 1000 characters`)
      len = 1000
    }

    const elementType = variable.type().templateArgument(0)
    mdb.log(`trying to return array of ${len} length of type ${elementType} at address ${address}`)
    return createArrayAt(BigInt(dataPtr), elementType, len, variable)
  }
)
