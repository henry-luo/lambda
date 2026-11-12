import s3: lambda.sync.s3
let a = s3.listing("<ListBucketResult xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\"><Prefix>spaces/abc/</Prefix><IsTruncated>true</IsTruncated><NextContinuationToken>opaque&amp;token</NextContinuationToken><Contents><Key>spaces/abc/catalog</Key><ETag>&quot;opaque-etag&quot;</ETag><Size>24</Size></Contents></ListBucketResult>","spaces/abc/")^
let b = s3.listing("<ListBucketResult><Prefix>spaces/abc/</Prefix><IsTruncated>false</IsTruncated></ListBucketResult>","spaces/abc/",a.next)^
let wrong = s3.listing("<ListBucketResult><Prefix>spaces/other/</Prefix><IsTruncated>false</IsTruncated></ListBucketResult>","spaces/abc/") ^ { ^ }
let truncated = s3.listing("<ListBucketResult><Prefix>spaces/abc/</Prefix><IsTruncated>true</IsTruncated></ListBucketResult>","spaces/abc/") ^ { ^ };
[a.next == "opaque&token",a.entries[0].etag == "\"opaque-etag\"",a.entries[0].size == 24,
 b.entries == [],b.next == null,wrong is error,truncated is error]
