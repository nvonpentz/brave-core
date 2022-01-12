extern crate feed_rs;
use lazy_static::lazy_static;
use feed_rs::parser;
use regex::Regex;

#[cxx::bridge(namespace = brave_news)] mod ffi {
  pub struct FeedItem {
    id: String,
    title: String,
    description: String,
    image_url: String,
    destionation_url: String,
    published_timestamp: i64,
  }

  pub struct FeedData {
    id: String,
    title: String,
    items: Vec<FeedItem>
  }

  extern "Rust" {
    fn parse_feed_string(source: String, output: &mut FeedData) -> bool;
  }
}

fn parse_feed_string(source: String, output: &mut ffi::FeedData) -> bool {
  lazy_static! {
    static ref IMAGE_REGEX: Regex = Regex::new("<img[^>]+src=\"([^\">]+)").unwrap();
  }
  // TODO(petemill): To sdee these errors, put `env_logger::init()` in an
  // 'init' function which only gets called once.
  let feed_result = parser::parse(source.as_bytes());
  if feed_result.is_err() {
    let error = feed_result.err().unwrap();
    let mut message = "Could not parse feed: ".to_owned();
    if matches!(error, parser::ParseFeedError::ParseError(_)) {
      message.push_str("ParseFeedError ");
    }
    else if matches!(error, parser::ParseFeedError::XmlReader(_)) {
      message.push_str("XMLReader ");
    }
    else {
      message.push_str("[unknown reason] ");
    }
    message.push_str(source.as_str());
    log::debug!("{}", message);
    return false;
  }
  let feed = feed_result.unwrap();
  output.title = voca_rs::strip::strip_tags(
    &(if feed.title.is_some() { feed.title.unwrap().content } else { String::from("") })
  );
  for feed_item_data in feed.entries {
    if feed_item_data.links.len() == 0 || feed_item_data.published.is_none() || (feed_item_data.title.is_none() && feed_item_data.summary.is_none()) || feed_item_data.published.is_none() {
      continue;
    }
    let mut image_url: String = String::from("");
    if feed_item_data.media.len() > 0 {
      for media_object in feed_item_data.media {
        if media_object.content.len() > 0 {
          let mut largest_width = 0;
          for content_item in media_object.content {
            if content_item.url.is_some() {
              let this_width = content_item.width.unwrap_or(0);
              if this_width >= largest_width {
                image_url = String::from(content_item.url.unwrap().as_str());
                largest_width = this_width;
              }
            }
          }
        }
        if image_url.is_empty() == false {
          continue;
        }
        if media_object.thumbnails.len() > 0 {
          let mut largest_width = 0;
          for content_item in media_object.thumbnails {
            if content_item.image.uri.is_empty() == false {
              let this_width = content_item.image.width.unwrap_or(0);
              if this_width >= largest_width {
                image_url = content_item.image.uri;
                largest_width = this_width;
              }
            }
          }
        }
      }
    }

    let mut summary: String = String::from("");
    if feed_item_data.summary.is_some() {
      summary = feed_item_data.summary.unwrap().content;
    } else if feed_item_data.content.is_some() {
      summary = feed_item_data.content.unwrap().body.unwrap_or(String::from(""));
    }
    if image_url.is_empty() == true && summary.is_empty() == false {
      // This relies on the string being already html-decoded, which
      // feed-rs (so far) does.
      let optional_caps = IMAGE_REGEX.captures(&summary);
      if optional_caps.is_some() {
        let caps = optional_caps.unwrap();
        if caps.len() > 1 {
          image_url = String::from(caps.get(1).unwrap().as_str());
        }
      }

    }
    let feed_item = ffi::FeedItem {
      id: feed_item_data.id,
      title: voca_rs::strip::strip_tags(&(if feed_item_data.title.is_some() { feed_item_data.title.unwrap().content } else { summary.clone() })),
      description: voca_rs::strip::strip_tags(&summary),
      image_url: image_url,
      destionation_url: feed_item_data.links[0].href.clone(),
      published_timestamp: feed_item_data.published.unwrap().timestamp()
    };
    output.items.push(feed_item);
  }
  return true;
}
