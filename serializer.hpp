#include <stdio.h>
#include "crc32.c"

struct Serializer
{
    enum class serialize_mode_t { Reading, Writing };

    FILE* fd;
    const char* m_filename;
    serialize_mode_t m_mode;

    uint32_t crc;

    uint64_t bytes_in_file      = 0;
    uint64_t position_in_file   = 0;

    uint32_t position_in_buffer = 0;
    uint32_t buffer_used        = 0;

    static const int FLUSH_GRANULARITY = 4096;
    static const int BUFFER_SIZE       = (FLUSH_GRANULARITY * 2);

    uint8_t buffer[BUFFER_SIZE];

    uint32_t r_invCrc; // reader only

private:
    void ReadFlush(void);
public:
    uint32_t ReadShortUint(void);
    uint32_t WriteFlush(void);


    Serializer(const char* filename, serialize_mode_t mode);
        
    ~Serializer()
    {
        // TODO maybe pad the file to a multiple of 4?
        if (m_mode == serialize_mode_t::Writing)
        {
            WriteFlush();
            fseek(fd, 8, SEEK_SET);
            fwrite(&crc, 1, sizeof(crc), fd);
            printf("writing crc: %x\n", crc);
            uint32_t invCrc = ~crc;
            fwrite(&invCrc, 1, sizeof(invCrc), fd);
            printf("writing invCrc: %x\n", invCrc);
        }
        if (m_mode == serialize_mode_t::Reading)
        {
            assert(position_in_file == bytes_in_file);
            assert(~crc == r_invCrc);
        }
        fclose(fd);
    }

    // Returns number of bytes written. 0 means error.
    uint8_t WriteShortUint(uint32_t value) {
        assert(m_mode == serialize_mode_t::Writing);

        if(value >= 0x40000000)
            return 0;

        if (position_in_buffer >= FLUSH_GRANULARITY)
        {
            // try to flush in 4092 chunks
            WriteFlush();
        }
        if (value < 0x80)
        {
            buffer[position_in_buffer++] = (uint8_t)value;
            return 1;
        }
        else if (value < (1 << 14))
        {
            buffer[position_in_buffer++] = (uint8_t)(value | 0x80);
            buffer[position_in_buffer++] = (uint8_t)(value >>  7);
            return 2;
        }
        else
        {
            buffer[position_in_buffer++] = (uint8_t)((value >> 0) | 0x80);
            buffer[position_in_buffer++] = (uint8_t)((value >> 7) | 0x80);
            buffer[position_in_buffer++] = (uint8_t)(value >> 14);
            buffer[position_in_buffer++] = (uint8_t)(value >> 22);
            return 4;
        }
    }

    /// May not write all the data in one go
    /// use in a loop or via the WRITE_ARRAY_DATA_SIZE macro
    /// Returns the number of bytes written
    uint32_t WriteRawData(const void* data, uint32_t size) {
        assert(m_mode == serialize_mode_t::Writing);

        if (position_in_buffer > FLUSH_GRANULARITY)
            WriteFlush();

        if (size > FLUSH_GRANULARITY)
            size = FLUSH_GRANULARITY;

        memcpy(buffer + position_in_buffer, data, size);
        position_in_buffer += size;
        assert(position_in_buffer < BUFFER_SIZE);

        return size;
    }

    /// May not read all the data in one go;
    /// use in a loop or via the READ_ARRAY_DATA_SIZE macro
    /// Returns the number of bytes read
    uint32_t ReadRawData(void* data, uint32_t size) {
        assert(m_mode == serialize_mode_t::Reading);

        if ((buffer_used - position_in_buffer) < FLUSH_GRANULARITY)
            ReadFlush();

        uint32_t bytes_avialable = (buffer_used - position_in_buffer);

        if (size > FLUSH_GRANULARITY)
            size = FLUSH_GRANULARITY;

        if (size > bytes_avialable)
            size = bytes_avialable;

        // printf("position in buffer");
        memcpy(data, buffer + position_in_buffer, size);
        position_in_buffer += size;

        assert(position_in_buffer < BUFFER_SIZE);

        return size;
    }

    uint32_t ReadU32(void) {
        assert(m_mode == serialize_mode_t::Reading);

        if (buffer_used - position_in_buffer < 4)
        {
            ReadFlush();
        }
        assert(buffer_used - position_in_buffer >= 4);
        uint32_t result =  (*(uint32_t*)(buffer + position_in_buffer));
        position_in_buffer += 4;
        return result;
    }

    void WriteU32(uint32_t value) {
        assert(m_mode == serialize_mode_t::Writing);

       if (position_in_buffer >= FLUSH_GRANULARITY)
       {
           WriteFlush();
       }

        (*(uint32_t*)(buffer + position_in_buffer)) = value;
        position_in_buffer += 4;
    }
} ;

#define WRITE_ARRAY_DATA(WRITER, ARRAY) \
    WRITE_ARRAY_DATA_SIZE(WRITER, ARRAY, sizeof(ARRAY))

#define READ_ARRAY_DATA(READER, ARRAY) \
    READ_ARRAY_DATA_SIZE(READER, ARRAY, sizeof(ARRAY))

#define WRITE_ARRAY_DATA_SIZE(WRITER, ARRAY, SIZE) \
    { \
        char* p = (char*)(ARRAY); \
        int bytes_left = (SIZE); \
        int bytes_written = 0; \
        do \
        { \
            bytes_written = (WRITER).WriteRawData(p, bytes_left); \
            p += bytes_written; \
            bytes_left -= bytes_written; \
        } while (bytes_written); \
        assert(bytes_left == 0); \
    }

#define READ_ARRAY_DATA_SIZE(READER, ARRAY, SIZE) \
    { \
        char* p = (char*)(ARRAY); \
        int bytes_left = (SIZE); \
        int bytes_read = 0; \
        do \
        { \
            bytes_read = (READER).ReadRawData(p, bytes_left); \
            p += bytes_read; \
            bytes_left -= bytes_read; \
        } while (bytes_read); \
        assert(bytes_left == 0); \
    }


void Serializer::ReadFlush(void)
{
    assert(buffer_used >= position_in_buffer); // general invariant

    const uint64_t bytes_available = bytes_in_file - position_in_file;
    const auto old_bytes_in_buffer = buffer_used - position_in_buffer;

    // assert(bytes_available > 0);

    uint32_t size_to_read = BUFFER_SIZE - buffer_used;
    if (bytes_available < size_to_read)
        size_to_read = bytes_available;

    memmove(buffer, buffer + position_in_buffer, old_bytes_in_buffer);
    auto bytes_read = fread(buffer + old_bytes_in_buffer, 1, size_to_read, fd);
    assert(bytes_read == size_to_read);

    crc = crc32c(crc, buffer + old_bytes_in_buffer, size_to_read);

    buffer_used = old_bytes_in_buffer + bytes_read;
    position_in_buffer = 0;

    position_in_file += size_to_read;
}

uint32_t Serializer::ReadShortUint(void) {
    assert(position_in_buffer < buffer_used);

    if ((buffer_used - position_in_buffer) < 4
        && bytes_in_file - position_in_file > 0)
    {
        ReadFlush();
    }

    assert(buffer_used > 1);

    auto mem = buffer + position_in_buffer;
    const auto first_byte = *mem++;
    position_in_buffer++;

    uint32_t result = (first_byte & 0x7f);
    const auto has_next = (first_byte & 0x80) != 0;
    if (has_next)
    {
        const auto second_byte = *mem++;
        position_in_buffer++;

        result |= ((second_byte & 0x7f) << 7);
        const auto has_next2 = (second_byte & 0x80) != 0;

        // read next byte
        if (has_next2)
        {
            result |= ((*(uint16_t*)mem) << 14);
            position_in_buffer += 2;
        }
    }

    return result;
}

uint32_t Serializer::WriteFlush(void)
{
    assert(m_mode == serialize_mode_t::Writing);

    uint32_t bytes_to_flush = FLUSH_GRANULARITY;
    if (position_in_buffer < bytes_to_flush)
        bytes_to_flush = position_in_buffer;

    crc = crc32c(crc, buffer, bytes_to_flush);
    fwrite(buffer, 1, bytes_to_flush, fd);

    position_in_file += bytes_to_flush;
    position_in_buffer -= bytes_to_flush;
    // cpy overhang
    memmove(buffer, buffer + bytes_to_flush, position_in_buffer);

    return bytes_to_flush;
}

Serializer::Serializer(const char* filename, serialize_mode_t mode) :
    m_filename(filename), m_mode(mode), crc(~0)  {
    fd = fopen(filename, m_mode == serialize_mode_t::Writing ? "wb" : "rb");

    if (!fd)
    {
        perror("Serializer()");
    }
    else
    {
        if (mode == serialize_mode_t::Writing)
        {
            fwrite("OSMb", 4, 1, fd); // write magic number
            uint32_t versionNumber = 1;
            if (ferror(fd))
            {
                perror("Serializer()");
                assert(0);
            }
            fwrite(&versionNumber, sizeof(versionNumber), 1, fd); // write version number
            // placeholder for ~crc;
            uint32_t invCrc = ~crc;
            // NOTE: crc is not valid yet we just write it as a place_holder
            fwrite(&crc, sizeof(crc), 1, fd);
            fwrite(&invCrc, sizeof(invCrc), 1, fd);
            assert(ftell(fd) == 16);
        }
        else
        {
            fseek(fd, 0, SEEK_END);
            bytes_in_file = ftell(fd);
            fseek(fd, 0, SEEK_SET);

            int bytes_read = 0;

            char magic[4];
            bytes_read += fread(&magic, 1, sizeof(magic), fd);

            uint32_t versionNumber;
            bytes_read += fread(&versionNumber, 1, sizeof(versionNumber), fd);

            uint32_t r_crc;
            bytes_read += fread(&r_crc, 1, sizeof(r_crc), fd);

            bytes_read += fread(&r_invCrc, 1, sizeof(r_invCrc), fd);
            if(r_crc != ~r_invCrc)
            {
                printf("read crc %x and invCrc %x \n", r_crc, r_invCrc);
                fprintf(stderr, "initial CRC check failed ... file '%s' is corrupted\n", m_filename);
                abort();
            }

            position_in_file = bytes_read;
            assert(ftell(fd) == 16 && position_in_file == 16);
            assert(0 == memcmp(&magic, "OSMb", 4));
        }
    }
}
